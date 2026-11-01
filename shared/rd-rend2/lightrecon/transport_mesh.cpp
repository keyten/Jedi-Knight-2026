// Disjoint planar arrangement over immutable source geometry, not baked sample folds.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "transport_mesh.h"
#include "bake_math.h"
#include "geometry_key.h"
#include <algorithm>
#include <functional>
#include <set>
#include <tuple>
namespace Rend2LightRecon
{
namespace Core
{
namespace Indirect
{
namespace Topology
{
using namespace Bake::Detail;
namespace
{
using Poly = std::vector<V>;
using Cell = std::tuple<int64_t, int64_t, int64_t>;
void Need(bool b, Code c, const char *s)
{
	if (!b)
		throw Failure(c, s);
}
void Cancel(const ReadContext &r)
{
	Need(!Cancelled(r), Code::Cancelled, "Transport topology cancelled");
}
double Area(const Poly &p)
{
	V sum;
	for (size_t i = 1; i + 1 < p.size(); ++i)
		sum = sum + Cross(p[i] - p[0], p[i + 1] - p[0]);
	return .5 * Length(sum);
}
double Epsilon(const Poly &a, const Poly &b = {})
{
	double scale = 1;
	for (const auto &p : {a, b})
		for (auto v : p)
			for (unsigned c = 0; c < 3; ++c)
				scale = std::max(scale, std::abs(v[c]));
	return 64 * std::numeric_limits<double>::epsilon() * scale;
}
double AreaRoundoff(const Poly &p)
{
	double perimeter = 0;
	for (size_t i = 0; i < p.size(); ++i)
		perimeter += Length(p[(i + 1) % p.size()] - p[i]);
	return Epsilon(p) * perimeter * 4;
}
Poly Clean(Poly p)
{
	double e = Epsilon(p);
	for (size_t i = 0; p.size() > 2 && i < p.size();)
	{
		size_t before = (i + p.size() - 1) % p.size(), after = (i + 1) % p.size();
		V a = p[i] - p[before], b = p[after] - p[i];
		if (Length(a) <= e || Length(b) <= e || Length(Cross(a, b)) <= e * (Length(a) + Length(b)))
		{
			p.erase(p.begin() + i);
			i = 0;
		}
		else
			++i;
	}
	return p;
}
Poly Clip(const Poly &p, V n, double d)
{
	Poly out;
	for (size_t i = 0; i < p.size(); ++i)
	{
		auto a = p[i], b = p[(i + 1) % p.size()];
		double x = Dot(n, a) - d, y = Dot(n, b) - d;
		if (x >= 0)
			out.push_back(a);
		if ((x >= 0) != (y >= 0))
			out.push_back(a + (b - a) * (x / (x - y)));
	}
	out = Clean(std::move(out));
	if (out.size() < 3 || Area(out) <= AreaRoundoff(out))
		return {};
	return out;
}
Poly Oriented(Poly p, V n)
{
	if (p.size() > 2 && Dot(Cross(p[1] - p[0], p[2] - p[0]), n) < 0)
		std::reverse(p.begin(), p.end());
	return p;
}
Poly Intersect(Poly p, const Poly &q, V n)
{
	for (size_t i = 0; i < q.size() && !p.empty(); ++i)
	{
		V edge = Cross(n, q[(i + 1) % q.size()] - q[i]);
		if (Length(edge) <= Epsilon(q))
			continue;
		p = Clip(p, edge, Dot(edge, q[i]));
	}
	return p;
}
std::vector<Poly> Subtract(Poly p, const Poly &q, V n)
{
	std::vector<Poly> out;
	for (size_t i = 0; i < q.size() && !p.empty(); ++i)
	{
		V edge = Cross(n, q[(i + 1) % q.size()] - q[i]);
		double d = Dot(edge, q[i]);
		if (Length(edge) <= Epsilon(q))
			continue;
		auto outside = Clip(p, edge * (-1), -d);
		if (!outside.empty())
			out.push_back(std::move(outside));
		p = Clip(p, edge, d);
	}
	return out;
}
bool BoxOverlap(const Poly &a, const Poly &b)
{
	for (unsigned c = 0; c < 3; ++c)
	{
		double lo = 1e300, hi = -1e300, otherLo = 1e300, otherHi = -1e300;
		for (auto v : a)
		{
			lo = std::min(lo, v[c]);
			hi = std::max(hi, v[c]);
		}
		for (auto v : b)
		{
			otherLo = std::min(otherLo, v[c]);
			otherHi = std::max(otherHi, v[c]);
		}
		double e = Epsilon(a, b);
		if (hi < otherLo - e || otherHi < lo - e)
			return false;
	}
	return true;
}
bool Coplanar(const Poly &a, V an, const Poly &b, V bn)
{
	if (std::abs(Dot(an, bn)) < 1 - 1e-12)
		return false;
	double e = Epsilon(a, b);
	for (auto p : a)
		if (std::abs(Dot(bn, p - b[0])) > e)
			return false;
	for (auto p : b)
		if (std::abs(Dot(an, p - a[0])) > e)
			return false;
	return true;
}
bool Interior(V x, const Poly &p, V n, double e)
{
	for (size_t i = 0; i < p.size(); ++i)
	{
		V edge = Cross(n, p[(i + 1) % p.size()] - p[i]);
		if (Dot(edge, x - p[i]) <= e * Length(edge))
			return false;
	}
	return true;
}
bool Crossing(const Poly &a, V an, const Poly &b, V bn)
{
	if (std::abs(Dot(an, bn)) > 1 - 1e-12)
		return false;
	std::vector<V> points;
	double eps = Epsilon(a, b);
	auto edges = [&](const Poly &p, V n, const Poly &q, V qn) {
		for (size_t i = 0; i < p.size(); ++i)
		{
			V x = p[i], y = p[(i + 1) % p.size()];
			double u = Dot(qn, x - q[0]), v = Dot(qn, y - q[0]);
			if ((u >= 0) == (v >= 0) || std::abs(u - v) <= eps)
				continue;
			V hit = x + (y - x) * (u / (u - v));
			bool inside = true;
			for (size_t j = 0; j < q.size(); ++j)
			{
				V e = Cross(qn, q[(j + 1) % q.size()] - q[j]);
				if (Dot(e, hit - q[j]) < -eps * Length(e))
					inside = false;
			}
			if (inside)
				points.push_back(hit);
		}
	};
	edges(a, an, b, bn);
	edges(b, bn, a, an);
	for (size_t i = 0; i < points.size(); ++i)
		for (size_t j = 0; j < i; ++j)
		{
			V mid = (points[i] + points[j]) * .5;
			if (Length(points[i] - points[j]) > eps && Interior(mid, a, an, eps) &&
				Interior(mid, b, bn, eps))
				return true;
		}
	return false;
}
Poly Polygon(const Bake::FootprintTriangle &t, Vec3 n)
{
	return Oriented({V(t.world[0]), V(t.world[1]), V(t.world[2])}, V(n));
}
template <class F> void Cells(const Poly &p, double size, F f)
{
	int64_t lo[3], hi[3];
	for (unsigned c = 0; c < 3; ++c)
	{
		double a = 1e300, b = -1e300;
		for (auto v : p)
		{
			a = std::min(a, v[c]);
			b = std::max(b, v[c]);
		}
		Need(std::abs(a / size) < 1e10 && std::abs(b / size) < 1e10, Code::InvalidInput,
			 "Topology spatial index range");
		lo[c] = int64_t(std::floor(a / size));
		hi[c] = int64_t(std::floor(b / size));
		Need(hi[c] - lo[c] <= 1024, Code::WorkLimit, "Topology spatial span budget");
	}
	for (int64_t x = lo[0]; x <= hi[0]; ++x)
		for (int64_t y = lo[1]; y <= hi[1]; ++y)
			for (int64_t z = lo[2]; z <= hi[2]; ++z)
				f(Cell(x, y, z));
}
Id Number(const Disk::Digest &d)
{
	Id v = 0;
	for (unsigned i = 0; i < 8; ++i)
		v |= Id(d[i]) << (8 * i);
	return v ? v : 1;
}
struct Piece
{
	Poly p;
	Support source;
	std::vector<Provenance> refs;
	bool active = true;
};
Disk::Digest PieceKey(const Piece &p)
{
	Core::GeometryKey k;
	k.U(p.source.material);
	k.U(p.source.style);
	k.V(p.source.normal);
	std::vector<std::tuple<double, double, double>> v;
	for (auto x : p.p)
		v.emplace_back(x.x, x.y, x.z);
	std::sort(v.begin(), v.end());
	for (auto x : v)
	{
		k.F(std::get<0>(x));
		k.F(std::get<1>(x));
		k.F(std::get<2>(x));
	}
	return k.Finish();
}
} // namespace
Contact Inspect(const Bake::FootprintTriangle &a, Vec3 an, const Bake::FootprintTriangle &b,
				Vec3 bn)
{
	Contact out;
	auto p = Polygon(a, an), q = Polygon(b, bn);
	if (Coplanar(p, V(an), q, V(bn)))
	{
		out.relation = Relation::Coplanar;
		q = Oriented(q, V(an));
		if (BoxOverlap(p, q))
			out.sharedArea = Area(Intersect(p, q, V(an)));
		if (out.sharedArea > Epsilon(p, q) * Epsilon(p, q))
			out.relation =
				Dot(V(an), V(bn)) > 0 ? Relation::SameSideOverlap : Relation::OppositeSideOverlap;
	}
	else if (BoxOverlap(p, q) && Crossing(p, V(an), q, V(bn)))
		out.relation = Relation::Crossing;
	return out;
}
Disk::Digest ReceiverKey(const Bake::Receiver &r)
{
	Core::GeometryKey k;
	k.U(kVersion);
	k.U(r.sample);
	k.U(r.surface);
	k.U(r.material);
	k.U(r.style);
	k.V(r.world);
	k.V(r.geometricNormal);
	k.V(r.shadingNormal);
	k.F(r.geometryErrorWorld);
	k.F(r.mappingErrorWorld);
	k.U(unsigned(r.domain));
	k.U(r.mappedPlanar);
	k.U(r.compilerPlacementCertified);
	k.U(r.twoSided);
	k.U(r.footprint.count);
	if (r.footprint.data)
		for (size_t i = 0; i < r.footprint.count; ++i)
			for (unsigned j = 0; j < 3; ++j)
			{
				k.V(r.footprint.data[i].world[j]);
				k.V(r.footprint.data[i].normal[j]);
			}
	return k.Finish();
}
Error Mesh::Build(Disk::Digest map, Disk::Digest geometry, std::vector<Support> input,
				  const Options &o, const ReadContext &read, std::shared_ptr<const Mesh> &out)
{
	out.reset();
	try
	{
		Cancel(read);
		Need(map != Disk::Digest{} && geometry != Disk::Digest{}, Code::InvalidInput,
			 "Topology identities required");
		Need(input.size() <= o.maxInputTriangles && o.maxInputTriangles <= 262144 && o.maxPieces &&
				 o.maxPieces <= 50000 && o.maxWork && o.maxBytes,
			 Code::MemoryLimit, "Topology input/piece limits");
		Need(std::isfinite(o.maximumEdgeWorld) && o.maximumEdgeWorld > 0, Code::InvalidInput,
			 "Topology edge policy");
		if (o.bounds.known)
			for (unsigned c = 0; c < 3; ++c)
				Need(Finite(o.bounds.minWorld) && Finite(o.bounds.maxWorld) &&
						 V(o.bounds.minWorld)[c] < V(o.bounds.maxWorld)[c],
					 Code::InvalidInput, "Topology bounds");
		auto m = std::make_shared<Mesh>();
		m->mapKey = map;
		m->geometryKey = geometry;
		m->options = o;
		auto &r = m->report;
		uint64_t cap = read.maxBytes ? std::min(read.maxBytes, o.maxBytes) : o.maxBytes;
		Need(input.size() * sizeof(Support) + 65536 <= cap, Code::MemoryLimit,
			 "Topology input allocation budget");
		auto step = [&](uint64_t n = 1) {
			Cancel(read);
			r.work += n;
			Need(r.work <= o.maxWork, Code::WorkLimit, "Topology arrangement work budget");
		};
		std::vector<Piece> seeds;
		std::function<void(Piece, unsigned)> split = [&](Piece p, unsigned depth) {
			step();
			Need(depth < 32, Code::Unavailable, "Topology subdivision precision exhausted");
			// Fan triangulation then longest-edge bisection: no nominal luxel size.
			if (p.p.size() > 3)
			{
				for (size_t i = 1; i + 1 < p.p.size(); ++i)
				{
					Piece t = p;
					t.p = {p.p[0], p.p[i], p.p[i + 1]};
					split(std::move(t), depth + 1);
				}
				return;
			}
			unsigned edge = 0;
			double longest = 0;
			for (unsigned i = 0; i < 3; ++i)
			{
				double d = Length(p.p[(i + 1) % 3] - p.p[i]);
				if (d > longest)
				{
					longest = d;
					edge = i;
				}
			}
			if (longest > o.maximumEdgeWorld)
			{
				V a = p.p[edge], b = p.p[(edge + 1) % 3], c = p.p[(edge + 2) % 3],
				  mid = (a + b) * .5;
				Piece q = p;
				p.p = {a, mid, c};
				q.p = {mid, b, c};
				split(std::move(p), depth + 1);
				split(std::move(q), depth + 1);
				return;
			}
			Need(seeds.size() < o.maxPieces, Code::MemoryLimit,
				 "Topology piece budget (change declared spatial scope)");
			seeds.push_back(std::move(p));
		};
		std::set<std::pair<Id, Id>> identities;
		for (const auto &s : input)
		{
			step();
			++r.inputTriangles;
			Need(s.surface && s.triangle && s.material &&
					 identities.emplace(s.surface, s.triangle).second,
				 Code::InvalidInput, "Duplicate/corrupt topology source identity");
			for (auto v : s.world)
				Need(Finite(v), Code::InvalidInput, "Nonfinite topology source");
			Poly p = {V(s.world[0]), V(s.world[1]), V(s.world[2])};
			double a = Area(p);
			r.inputArea += a;
			Need(a > 0 && std::isfinite(a), Code::InvalidInput, "Degenerate topology source");
			if (o.bounds.known)
				for (unsigned c = 0; c < 3; ++c)
				{
					V n;
					n[c] = 1;
					p = Clip(p, n, V(o.bounds.minWorld)[c]);
					p = Clip(p, n * (-1), -V(o.bounds.maxWorld)[c]);
				}
			if (p.empty())
				continue;
			r.scopedArea += Area(p);
			++r.clippedTriangles;
			if (!s.sideKnown || !Normal(s.normal))
			{
				r.unsupportedArea += Area(p);
				++r.unsupportedRecords;
				r.issues |= UnknownSide;
				continue;
			}
			Need(std::abs(
					 Dot(Unit(Cross(V(s.world[1]) - V(s.world[0]), V(s.world[2]) - V(s.world[0]))),
						 V(s.normal))) > 1 - 1e-6,
				 Code::InvalidInput, "Topology normal/plane mismatch");
			Piece q;
			q.p = Oriented(p, V(s.normal));
			q.source = s;
			q.refs.push_back({s.surface, s.triangle});
			split(std::move(q), 0);
		}
		r.splitPieces = seeds.size();
		std::sort(seeds.begin(), seeds.end(), [](const Piece &a, const Piece &b) {
			auto x = PieceKey(a), y = PieceKey(b);
			if (x != y)
				return x < y;
			return std::tie(a.source.surface, a.source.triangle) <
				   std::tie(b.source.surface, b.source.triangle);
		});
		std::vector<Piece> pieces, excluded;
		std::map<Cell, std::vector<size_t>> buckets;
		uint64_t referenceBytes = 0;
		auto insert = [&](Piece p) {
			if (p.p.size() > 64)
			{
				r.issues |= NumericalGeometry;
				r.unsupportedArea += Area(p.p);
				++r.unsupportedRecords;
				return;
			}
			Need(pieces.size() < o.maxPieces * 8, Code::MemoryLimit,
				 "Topology arrangement fragmentation budget");
			referenceBytes += p.refs.size() * sizeof(Provenance) * 4;
			Need((pieces.size() + seeds.size() + excluded.size() + 1) * 2048ull +
						 input.size() * sizeof(Support) + referenceBytes + 65536 <=
					 cap,
				 Code::MemoryLimit, "Topology live insertion budget");
			size_t i = pieces.size();
			pieces.push_back(std::move(p));
			Cells(pieces.back().p, o.maximumEdgeWorld, [&](Cell c) {
				step();
				buckets[c].push_back(i);
			});
		};
		for (auto seed : seeds)
		{
			std::vector<Poly> pending{seed.p};
			for (const auto &bad : excluded)
			{
				step();
				if (Dot(V(seed.source.normal), V(bad.source.normal)) < 0 ||
					!BoxOverlap(seed.p, bad.p) ||
					!Coplanar(seed.p, V(seed.source.normal), bad.p, V(bad.source.normal)))
					continue;
				std::vector<Poly> next;
				for (auto p : pending)
				{
					step();
					auto q =
						Subtract(p, Oriented(bad.p, V(seed.source.normal)), V(seed.source.normal));
					double retained = 0;
					for (const auto &part : q)
						retained += Area(part);
					r.unsupportedArea += std::max(0., Area(p) - retained);
					Need(next.size() + q.size() <= o.maxPieces, Code::MemoryLimit,
						 "Topology pending-piece budget");
					next.insert(next.end(), q.begin(), q.end());
				}
				pending = std::move(next);
			}
			std::set<size_t> candidates;
			Cells(seed.p, o.maximumEdgeWorld, [&](Cell c) {
				step();
				auto it = buckets.find(c);
				if (it != buckets.end())
					candidates.insert(it->second.begin(), it->second.end());
			});
			for (size_t index : candidates)
			{
				step();
				if (!pieces[index].active || pending.empty())
					continue;
				Piece existing = pieces[index];
				if (!BoxOverlap(seed.p, existing.p))
					continue;
				V n(seed.source.normal), en(existing.source.normal);
				if (!Coplanar(seed.p, n, existing.p, en))
					continue;
				if (Dot(n, en) < 0)
				{
					if (Area(Intersect(seed.p, Oriented(existing.p, n), n)) > 0)
						++r.opposingPairs;
					continue;
				}
				std::vector<Poly> next, existingRemainder{existing.p};
				bool conflict = false;
				for (auto p : pending)
				{
					step();
					auto overlap = Intersect(p, existing.p, n);
					double a = Area(overlap);
					if (a <= AreaRoundoff(overlap))
					{
						next.push_back(p);
						continue;
					}
					if (seed.source.material == existing.source.material &&
						seed.source.style == existing.source.style)
					{
						++r.duplicateOverlaps;
						r.deduplicatedArea += a;
						referenceBytes += seed.refs.size() * sizeof(Provenance) * 4;
						Need((pieces.size() + seeds.size() + excluded.size()) * 2048ull +
									 input.size() * sizeof(Support) + referenceBytes + 65536 <=
								 cap,
							 Code::MemoryLimit, "Topology provenance budget");
						pieces[index].refs.insert(pieces[index].refs.end(), seed.refs.begin(),
												  seed.refs.end());
					}
					else
					{
						++r.conflictingOverlaps;
						r.issues |= ConflictingMaterial;
						r.unsupportedArea += 2 * a;
						conflict = true;
						std::vector<Poly> remainder;
						for (auto poly : existingRemainder)
						{
							auto parts = Subtract(poly, p, n);
							Need(remainder.size() + parts.size() <= o.maxPieces, Code::MemoryLimit,
								 "Topology difference budget");
							remainder.insert(remainder.end(), parts.begin(), parts.end());
						}
						existingRemainder = std::move(remainder);
						Piece bad = seed;
						bad.p = overlap;
						excluded.push_back(std::move(bad));
					}
					auto remaining = Subtract(p, existing.p, n);
					Need(next.size() + remaining.size() <= o.maxPieces, Code::MemoryLimit,
						 "Topology pending-piece budget");
					next.insert(next.end(), remaining.begin(), remaining.end());
				}
				if (conflict)
				{
					pieces[index].active = false;
					for (auto poly : existingRemainder)
					{
						Piece e = existing;
						e.p = std::move(poly);
						insert(std::move(e));
					}
				}
				pending = std::move(next);
			}
			for (auto p : pending)
			{
				Piece q = seed;
				q.p = std::move(p);
				insert(std::move(q));
			}
			Need((pieces.size() + seeds.size() + excluded.size() + pending.size()) * 2048ull +
						 input.size() * sizeof(Support) + referenceBytes + 65536 <=
					 cap,
				 Code::MemoryLimit, "Topology live arrangement budget");
		}
		// Non-coplanar interior crossings are not a material surface arrangement.
		// Omit only the affected bounded pieces, retaining unrelated support.
		std::set<size_t> crossing;
		for (size_t i = 0; i < pieces.size(); ++i)
			if (pieces[i].active)
			{
				std::set<size_t> candidates;
				Cells(pieces[i].p, o.maximumEdgeWorld, [&](Cell c) {
					step();
					auto it = buckets.find(c);
					if (it != buckets.end())
						candidates.insert(it->second.begin(), it->second.end());
				});
				for (size_t j : candidates)
					if (j < i && pieces[j].active)
					{
						step();
						if (BoxOverlap(pieces[i].p, pieces[j].p) &&
							Crossing(pieces[i].p, V(pieces[i].source.normal), pieces[j].p,
									 V(pieces[j].source.normal)))
						{
							++r.crossingPairs;
							crossing.insert(i);
							crossing.insert(j);
						}
					}
			}
		for (size_t i : crossing)
		{
			pieces[i].active = false;
			r.unsupportedArea += Area(pieces[i].p);
			r.issues |= CrossingGeometry;
		}
		std::set<Id> ids;
		for (auto &p : pieces)
			if (p.active)
			{
				step();
				Element e;
				auto &v = e.patch;
				v.material = p.source.material;
				v.receiver.material = v.material;
				v.receiver.surface = p.source.surface;
				v.receiver.style = p.source.style;
				v.receiver.geometricNormal = v.receiver.shadingNormal = p.source.normal;
				v.receiver.mappedPlanar = true;
				// These are conditional incident probes, not claimed original compiler luxel
				// origins.
				v.receiver.compilerPlacementCertified = true;
				auto f = std::make_shared<std::vector<Bake::FootprintTriangle>>();
				V centroid;
				for (size_t i = 1; i + 1 < p.p.size(); ++i)
				{
					Bake::FootprintTriangle t;
					V vs[3] = {p.p[0], p.p[i], p.p[i + 1]};
					for (unsigned c = 0; c < 3; ++c)
					{
						t.world[c] = vs[c].Float();
						t.normal[c] = p.source.normal;
						v.receiver.geometryErrorWorld =
							std::max(v.receiver.geometryErrorWorld,
									 std::abs(Dot(V(p.source.normal), V(t.world[c]) - p.p[0])));
					}
					double a = Area({V(t.world[0]), V(t.world[1]), V(t.world[2])});
					if (a <= Epsilon(p.p) * Epsilon(p.p))
						continue;
					v.areaWorldSquared += a;
					centroid = centroid + (V(t.world[0]) + V(t.world[1]) + V(t.world[2])) * (a / 3);
					f->push_back(t);
				}
				if (f->empty())
				{
					r.issues |= NumericalGeometry;
					r.unsupportedArea += Area(p.p);
					continue;
				}
				if (v.receiver.geometryErrorWorld <= Epsilon(p.p))
					v.receiver.geometryErrorWorld = 0;
				v.receiver.world = (centroid / v.areaWorldSquared).Float();
				v.receiver.footprint = VectorChunk<Bake::FootprintTriangle>(f, 0, f->size());
				std::sort(
					p.refs.begin(), p.refs.end(), [](const Provenance &a, const Provenance &b) {
						return std::tie(a.surface, a.triangle) < std::tie(b.surface, b.triangle);
					});
				p.refs.erase(std::unique(p.refs.begin(), p.refs.end(),
										 [](const Provenance &a, const Provenance &b) {
											 return a.surface == b.surface &&
													a.triangle == b.triangle;
										 }),
							 p.refs.end());
				e.sources = std::move(p.refs);
				Core::GeometryKey id;
				id.U(kVersion);
				id.Digest(map);
				id.Digest(geometry);
				id.Digest(PieceKey(p));
				v.id = Number(id.Finish());
				v.receiver.sample = v.id;
			Need(ids.insert(v.id).second,Code::AlgorithmError,"Topology stable ID collision");
				r.outputArea += v.areaWorldSquared;
				m->elements.push_back(std::move(e));
			}
		std::sort(m->elements.begin(), m->elements.end(),
				  [](const Element &a, const Element &b) { return a.patch.id < b.patch.id; });
		// The canonical receiver footprint uses float coordinates. Quantizing an
		// exact double arrangement can create a sliver overlap. Do not silently
		// double-count it or enlarge a geometric tolerance: omit the local pieces
		// whose disjoint support cannot be represented by this footprint format.
		std::map<Cell, std::vector<size_t>> quantizedIndex;
		std::set<size_t> imprecise;
		for (size_t i = 0; i < m->elements.size(); ++i)
		{
			const auto &a = m->elements[i].patch;
			Poly bound;
			for (size_t t = 0; t < a.receiver.footprint.count; ++t)
				for (auto v : a.receiver.footprint.data[t].world)
					bound.push_back(V(v));
			std::set<size_t> candidates;
			Cells(bound, o.maximumEdgeWorld, [&](Cell c) {
				step();
				auto found = quantizedIndex.find(c);
				if (found != quantizedIndex.end())
					candidates.insert(found->second.begin(), found->second.end());
			});
			for (size_t j : candidates)
			{
				step();
				const auto &b = m->elements[j].patch;
				for (size_t x = 0; x < a.receiver.footprint.count; ++x)
					for (size_t y = 0; y < b.receiver.footprint.count; ++y)
					{
						auto contact =
							Inspect(a.receiver.footprint.data[x], a.receiver.geometricNormal,
									b.receiver.footprint.data[y], b.receiver.geometricNormal);
						if (contact.relation == Relation::SameSideOverlap &&
							contact.sharedArea >
								1e-10 * std::min(a.areaWorldSquared, b.areaWorldSquared))
						{
							imprecise.insert(i);
							imprecise.insert(j);
						}
					}
			}
			Cells(bound, o.maximumEdgeWorld, [&](Cell c) {
				step();
				quantizedIndex[c].push_back(i);
			});
		}
		std::vector<Element> representable;
		for (size_t i = 0; i < m->elements.size(); ++i)
		{
			if (imprecise.count(i))
			{
				++r.precisionExcludedPieces;
				r.issues |= NumericalGeometry;
				r.unsupportedArea += m->elements[i].patch.areaWorldSquared;
				r.outputArea -= m->elements[i].patch.areaWorldSquared;
			}
			else
				representable.push_back(std::move(m->elements[i]));
		}
		m->elements = std::move(representable);
		Core::GeometryKey key;
		key.U(kVersion);
		key.Digest(map);
		key.Digest(geometry);
		key.B(o.bounds);
		key.F(o.maximumEdgeWorld);
		key.U(o.completeSourceSet);
		for (size_t i = 0; i < m->elements.size(); ++i)
		{
			const auto &e = m->elements[i];
			key.U(e.patch.id);
			key.Digest(ReceiverKey(e.patch.receiver));
			for (auto ref : e.sources)
			{
				key.U(ref.surface);
				key.U(ref.triangle);
			}
			Poly p;
			for (size_t t = 0; t < e.patch.receiver.footprint.count; ++t)
				for (auto v : e.patch.receiver.footprint.data[t].world)
					p.push_back(V(v));
			Cells(p, o.maximumEdgeWorld, [&](Cell c) {
				step();
				m->index[c].push_back(i);
			});
		}
		r.areaAccountingResidual =
			r.scopedArea - r.outputArea - r.deduplicatedArea - r.unsupportedArea;
		if (std::abs(r.areaAccountingResidual) > 1e-6 * std::max(1., r.scopedArea))
			r.issues |= NumericalGeometry;
		key.U(r.issues);
		key.F(r.unsupportedArea);
		key.F(r.areaAccountingResidual);
		m->key = key.Finish();
		r.complete = o.completeSourceSet && !o.bounds.known && !r.issues;
		r.bytes = input.size() * sizeof(Support) +
				  (pieces.size() + seeds.size() + excluded.size()) * 2048ull + referenceBytes +
				  65536;
		Need(r.bytes <= cap, Code::MemoryLimit, "Topology final memory budget");
		out = m;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Topology allocation");
	}
}
Error Mesh::FromSurfaces(const SurfaceGeometry &g, Disk::Digest geometry, const Options &o,
						 const ReadContext &read, std::shared_ptr<const Mesh> &out)
{
	out.reset();
	try
	{
		std::vector<Support> supports;
		uint64_t unsupported = 0;
		uint32_t issues = 0;
		for (const auto &s : g.Surfaces())
		{
			Cancel(read);
			if (s.motion != SurfaceMotion::StaticWorld || s.kind == BakedSurfaceKind::Flare ||
				s.excludedMaterial)
				continue;
			if ((s.kind != BakedSurfaceKind::Face && s.kind != BakedSurfaceKind::Triangles) ||
				!s.materialFacts.available || s.materialFacts.cull == SurfaceCull::Unknown ||
				(s.materialFacts.signals & (MaterialSky | MaterialCutout | MaterialTranslucent |
											MaterialNoDraw | MaterialDeformed)))
			{
				++unsupported;
				issues |= UnsupportedSurface;
				continue;
			}
			Need(s.indices.size() % 3 == 0, Code::InvalidInput,
				 "Malformed source triangle index count");
			for (size_t i = 0; i < s.indices.size(); i += 3)
			{
				Support v;
				v.surface = s.ordinal + 1;
				v.triangle = i / 3 + 1;
				v.material = s.materialIndex + 1;
				V average;
				for (unsigned c = 0; c < 3; ++c)
				{
					Need(s.indices[i + c] < s.vertices.size(), Code::InvalidInput,
						 "Source triangle index range");
					const auto &p = s.vertices[s.indices[i + c]];
					v.world[c] = p.position;
					average = average + V(p.shadingNormal);
				}
				V normal =
					Unit(Cross(V(v.world[1]) - V(v.world[0]), V(v.world[2]) - V(v.world[0])));
				if (Length(normal) < .5)
				{
					++unsupported;
					issues |= NumericalGeometry;
					continue;
				}
				if (Dot(normal, average) < 0)
					normal = normal * (-1);
				v.normal = normal.Float();
				v.sideKnown = Length(average) > .5;
				for (unsigned c = 0; c < 3; ++c)
					v.sideKnown &=
						Length(V(s.vertices[s.indices[i + c]].shadingNormal) - normal) < 1e-4;
				Need(supports.size() < o.maxInputTriangles, Code::MemoryLimit,
					 "Source topology triangle limit");
				supports.push_back(v);
				// The captured constant normal selects the known receiver side. A second
				// side needs its own captured surface/explicit Support, not a cull-mode guess.
			}
		}
		std::shared_ptr<const Mesh> result;
		auto error = Build(g.Map().sha256, geometry, std::move(supports), o, read, result);
		if (!error)
			return error;
		auto m = std::const_pointer_cast<Mesh>(result);
		m->report.unsupportedRecords += unsupported;
		m->report.issues |= issues;
		m->report.complete &= !unsupported;
		Core::GeometryKey key;
		key.Digest(m->key);
		key.U(unsupported);
		key.U(issues);
		m->key = key.Finish();
		out = m;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Source topology allocation");
	}
}
Error Mesh::Map(const Bake::Receiver &r, const ReadContext &read,
				std::shared_ptr<const Mapping> &out) const
{
	out.reset();
	try
	{
		Cancel(read);
		Need(r.sample && r.material && Finite(r.world) && Normal(r.geometricNormal) &&
				 Normal(r.shadingNormal) && std::isfinite(r.geometryErrorWorld) &&
				 r.geometryErrorWorld >= 0 && std::isfinite(r.mappingErrorWorld) &&
				 r.mappingErrorWorld >= 0 && r.footprint.owner && r.footprint.data &&
				 r.footprint.count && r.footprint.count <= 64,
			 Code::InvalidInput, "Mapping receiver geometry required");
		auto m = std::make_shared<Mapping>();
		m->observation = r.sample;
		m->meshKey = key;
		m->receiverKey = Topology::ReceiverKey(r);
		std::map<Id, double> weights;
		uint64_t work = 0;
		for (size_t t = 0; t < r.footprint.count; ++t)
		{
			for (auto v : r.footprint.data[t].world)
				Need(Finite(v), Code::InvalidInput, "Nonfinite receiver footprint");
			auto p = Polygon(r.footprint.data[t], r.geometricNormal);
			m->area += Area(p);
			std::set<size_t> candidates;
			if (r.twoSided)
			{
				m->issues |= UnknownSide;
				continue;
			}
			Cells(p, options.maximumEdgeWorld, [&](Cell c) {
				Cancel(read);
				Need(++work <= options.maxWork, Code::WorkLimit, "Footprint spatial query budget");
				auto it = index.find(c);
				if (it != index.end())
					candidates.insert(it->second.begin(), it->second.end());
			});
			for (size_t i : candidates)
			{
				Cancel(read);
				Need(++work <= options.maxWork, Code::WorkLimit, "Footprint mapping work budget");
				const auto &e = elements[i];
				const auto &v = e.patch;
				const auto &s = v.receiver;
				if (v.material != r.material || s.style != r.style ||
					Dot(V(s.geometricNormal), V(r.geometricNormal)) < 1 - 1e-8)
					continue;
				for (size_t j = 0; j < s.footprint.count; ++j)
				{
					auto q = Polygon(s.footprint.data[j], s.geometricNormal);
					if (!BoxOverlap(p, q))
						continue;
					if (!Coplanar(p, V(r.geometricNormal), q, V(s.geometricNormal)))
						continue;
					double a = Area(Intersect(p, q, V(r.geometricNormal)));
					if (a > 0)
						weights[v.id] += a;
				}
			}
		}
		Need(m->area > 0 && std::isfinite(m->area), Code::InvalidInput,
			 "Invalid mapping footprint area");
		for (auto w : weights)
		{
			m->weights.push_back({w.first, w.second});
			m->coveredArea += w.second;
		}
		Need(m->coveredArea <= m->area + 1e-6 * std::max(1., m->area), Code::InvalidState,
			 "Mapping has duplicate physical coverage");
		if (m->coveredArea >= m->area - 1e-6 * std::max(1., m->area))
			m->state = Availability::Available;
		else
		{
			m->state = m->coveredArea > 0 ? Availability::Partial : Availability::Unavailable;
			m->issues |= PartialCoverage;
			if (!m->coveredArea)
				m->issues |= OutsideSupport;
		}
		out = m;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Footprint mapping allocation");
	}
}
} // namespace Topology
} // namespace Indirect
} // namespace Core
} // namespace Rend2LightRecon
