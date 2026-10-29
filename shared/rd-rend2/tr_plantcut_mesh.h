/*
 * Mesh cutting of the MD3 plant saber cut (tr_plantcut.cpp), without any
 * renderer dependency: tools/rend2/plantcut_mesh tests it on the CPU.
 *
 * The mesh is the rest geometry of the plant surfaces (object space, with UV,
 * normal, tangent and the frond parameter of every vertex) and the world
 * position of every vertex in the pose being cut. A cut is a triangle of the
 * blade sweep (the blade at the start and at the end of a step, world space):
 *   - a mesh triangle the sweep plane crosses gets the crossing segment, which
 *     is clipped to the sweep triangle;
 *   - crossed all the way: the triangle is split into its two sides, the
 *     vertices on the cut are duplicated per side (the sides come apart);
 *   - crossed from one edge to a point inside (the blade ends in the
 *     triangle): a slit, the edge vertex is duplicated per side and the tip is
 *     one vertex shared by both (cloth cut of cg_cloth.cpp, in 3D);
 *   - a stab through the middle of a triangle cuts nothing.
 * New vertices interpolate the rest attributes with the fraction found on the
 * world positions. Connectivity goes through welded positions (stock MD3s
 * split the UV seams), so a split side is one piece across a UV seam.
 */
#pragma once

#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstdint>

namespace plantcut
{

struct V3
{
	float x, y, z;
};
inline V3 MakeV3(float x, float y, float z) { V3 r = { x, y, z }; return r; }
inline V3 operator+(V3 a, V3 b) { return MakeV3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline V3 operator-(V3 a, V3 b) { return MakeV3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline V3 operator*(V3 a, float s) { return MakeV3(a.x * s, a.y * s, a.z * s); }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return MakeV3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline float Length(V3 a) { return sqrtf(Dot(a, a)); }
inline V3 Lerp(V3 a, V3 b, float t) { return a + (b - a) * t; }

struct Vertex
{
	V3 rest;			// object space
	V3 normal;			// object space
	float st[2];
	float tangent[4];	// object space, w = bitangent sign
	int frond;			// skeleton frond, -1 none (hub, stem: rest)
	float u;			// along the frond, node units (0 = attachment)
	int weld;			// welded position (connectivity)
	bool anchor;		// holds the piece to the plant (hub, frond base)
};

struct Triangle
{
	int v[3];
	int surface;		// model surface
};

struct Mesh
{
	std::vector<Vertex> verts;
	std::vector<V3> world;		// pose being cut, per vertex
	std::vector<Triangle> tris;
	int nextWeld = 0;
};

struct CutStats
{
	int split = 0;		// triangles crossed all the way
	int slit = 0;		// triangles the blade ends in
};

// attributes of a vertex between a and b (fraction t from a)
inline Vertex LerpVertex(const Vertex &a, const Vertex &b, float t)
{
	Vertex r;
	r.rest = Lerp(a.rest, b.rest, t);
	V3 n = Lerp(a.normal, b.normal, t);
	const float nl = Length(n);
	r.normal = nl > 1e-6f ? n * (1.0f / nl) : (t < 0.5f ? a.normal : b.normal);
	for (int i = 0; i < 2; ++i)
		r.st[i] = a.st[i] + (b.st[i] - a.st[i]) * t;
	V3 tg = Lerp(MakeV3(a.tangent[0], a.tangent[1], a.tangent[2]), MakeV3(b.tangent[0], b.tangent[1], b.tangent[2]), t);
	const float tl = Length(tg);
	const Vertex &closest = t < 0.5f ? a : b;
	if (tl > 1e-6f)
		tg = tg * (1.0f / tl);
	else
		tg = MakeV3(closest.tangent[0], closest.tangent[1], closest.tangent[2]);
	r.tangent[0] = tg.x; r.tangent[1] = tg.y; r.tangent[2] = tg.z;
	r.tangent[3] = closest.tangent[3];
	// the frond parameter: a hub / stem vertex is the attachment (u 0) of the
	// frond on the other end; across two fronds the nearer one
	if (a.frond == b.frond)
	{
		r.frond = a.frond;
		r.u = a.u + (b.u - a.u) * t;
	}
	else if (a.frond < 0)
	{
		r.frond = b.frond;
		r.u = b.u * t;
	}
	else if (b.frond < 0)
	{
		r.frond = a.frond;
		r.u = a.u * (1.0f - t);
	}
	else
	{
		r.frond = closest.frond;
		r.u = closest.u;
	}
	r.weld = -1;
	r.anchor = false;
	return r;
}

// attributes at barycentric weights w of the triangle a, b, c
inline Vertex BaryVertex(const Vertex &a, const Vertex &b, const Vertex &c, const float w[3])
{
	// two lerps: (a, b) by w1 / (w0 + w1), then towards c by w2
	const float ab = w[0] + w[1];
	const Vertex e = ab > 1e-6f ? LerpVertex(a, b, w[1] / ab) : a;
	return LerpVertex(e, c, w[2]);
}

namespace detail
{

struct KeyHash
{
	size_t operator()(uint64_t k) const { return (size_t)(k ^ (k >> 29) ^ (k >> 47)) * 0x9E3779B97F4A7C15ull; }
};

// lo <= hi < 2^31, side -1 / +1
inline uint64_t EdgeKey(int lo, int hi, int side)
{
	return ((uint64_t)(uint32_t)lo << 32) | ((uint64_t)((uint32_t)hi & 0x7fffffffu) << 1) | (side > 0 ? 1u : 0u);
}

struct Cutter
{
	Mesh &m;
	V3 n;					// plane normal
	V3 origin;
	std::vector<float> dist;	// signed distance per vertex (grows with new vertices)
	std::unordered_map<uint64_t, int, KeyHash> seam;	// (vertex lo, hi, side) -> vertex
	std::unordered_map<uint64_t, int, KeyHash> seamWeld;	// (weld lo, hi, side) -> weld

	Cutter(Mesh &mesh, V3 normal, V3 o) : m(mesh), n(normal), origin(o)
	{
		dist.resize(m.verts.size());
		for (size_t i = 0; i < m.verts.size(); ++i)
			dist[i] = Dot(m.world[i] - origin, n);
	}

	int Side(int v) const { return dist[v] >= 0.0f ? 1 : -1; }

	int AddVertex(const Vertex &vx, V3 world)
	{
		m.verts.push_back(vx);
		m.world.push_back(world);
		dist.push_back(Dot(world - origin, n));
		return (int)m.verts.size() - 1;
	}

	// the vertex of the given side where the plane crosses the edge a-b (a
	// vertex on the plane: a == b side of it)
	int SeamVertex(int a, int b, int side)
	{
		int lo = a, hi = b;
		if (lo > hi)
		{
			lo = b;
			hi = a;
		}
		const uint64_t key = EdgeKey(lo, hi, side);
		auto it = seam.find(key);
		if (it != seam.end())
			return it->second;
		float t = 0.0f;
		if (lo != hi)
		{
			const float d0 = dist[lo], d1 = dist[hi];
			t = d0 != d1 ? d0 / (d0 - d1) : 0.5f;
			t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
		}
		Vertex vx = LerpVertex(m.verts[lo], m.verts[hi], t);
		int wlo = m.verts[lo].weld, whi = m.verts[hi].weld;
		if (wlo > whi)
		{
			const int w = wlo;
			wlo = whi;
			whi = w;
		}
		const uint64_t wkey = EdgeKey(wlo, whi, side);
		auto wit = seamWeld.find(wkey);
		if (wit == seamWeld.end())
			wit = seamWeld.emplace(wkey, m.nextWeld++).first;
		vx.weld = wit->second;
		// the plane goes through the vertex: its side keeps it
		const V3 world = Lerp(m.world[lo], m.world[hi], t);
		const int index = AddVertex(vx, world);
		dist[index] = 0.0f;
		seam.emplace(key, index);
		return index;
	}

	static bool Degenerate(const Mesh &m, int a, int b, int c)
	{
		if (a == b || b == c || c == a)
			return true;
		const V3 cr = Cross(m.verts[b].rest - m.verts[a].rest, m.verts[c].rest - m.verts[a].rest);
		return Dot(cr, cr) < 1e-10f;
	}

	void Emit(std::vector<Triangle> &out, int a, int b, int c, int surface) const
	{
		if (Degenerate(m, a, b, c))
			return;
		Triangle t = { { a, b, c }, surface };
		out.push_back(t);
	}

	// the part of the triangle on the wanted side, fanned
	void ClipSide(const Triangle &tri, int wanted, std::vector<Triangle> &out)
	{
		int poly[4];
		int count = 0;
		for (int e = 0; e < 3; ++e)
		{
			const int prev = tri.v[(e + 2) % 3];
			const int cur = tri.v[e];
			const bool prevIn = Side(prev) == wanted;
			const bool curIn = Side(cur) == wanted;
			if (prevIn != curIn)
				poly[count++] = SeamVertex(prev, cur, wanted);
			// a vertex on the plane counts as the positive side; the negative
			// side gets its own copy through the crossing of the edge into it
			if (curIn)
				poly[count++] = cur;
		}
		for (int k = 1; k + 1 < count; ++k)
			Emit(out, poly[0], poly[k], poly[k + 1], tri.surface);
	}

	// the blade enters through edge (e, e + 1) and ends at tip inside
	void Slit(const Triangle &tri, int e, V3 tipWorld, std::vector<Triangle> &out)
	{
		const int i0 = tri.v[e], i1 = tri.v[(e + 1) % 3], i2 = tri.v[(e + 2) % 3];
		// barycentric weights of the tip in the world triangle
		const V3 p0 = m.world[i0], p1 = m.world[i1], p2 = m.world[i2];
		const V3 v0 = p1 - p0, v1 = p2 - p0, v2 = tipWorld - p0;
		const float d00 = Dot(v0, v0), d01 = Dot(v0, v1), d11 = Dot(v1, v1);
		const float d20 = Dot(v2, v0), d21 = Dot(v2, v1);
		const float den = d00 * d11 - d01 * d01;
		if (fabsf(den) < 1e-12f)
		{
			out.push_back(tri);
			return;
		}
		float w[3];
		w[1] = (d11 * d20 - d01 * d21) / den;
		w[2] = (d00 * d21 - d01 * d20) / den;
		w[0] = 1.0f - w[1] - w[2];
		for (int k = 0; k < 3; ++k)
			w[k] = w[k] < 0.0f ? 0.0f : w[k];
		const float sum = w[0] + w[1] + w[2];
		for (int k = 0; k < 3; ++k)
			w[k] /= sum;
		Vertex tip = BaryVertex(m.verts[i0], m.verts[i1], m.verts[i2], w);
		tip.weld = m.nextWeld++;
		const int t = AddVertex(tip, p0 * w[0] + p1 * w[1] + p2 * w[2]);
		const int s0 = Side(i0);
		const int a = SeamVertex(i0, i1, s0);
		const int b = SeamVertex(i0, i1, -s0);
		Emit(out, i0, a, t, tri.surface);
		Emit(out, b, i1, t, tri.surface);
		Emit(out, i1, i2, t, tri.surface);
		Emit(out, i2, i0, t, tri.surface);
	}
};

} // namespace detail

/*
=============
CutByTriangle

The mesh cut by the sweep triangle A, B, C (world). Returns whether anything
was cut.
=============
*/
inline bool CutByTriangle(Mesh &m, V3 A, V3 B, V3 C, CutStats *stats = nullptr)
{
	V3 n = Cross(B - A, C - A);
	const float area2 = Length(n);
	if (area2 < 1e-3f)
		return false;
	n = n * (1.0f / area2);
	// in-plane frame: the sweep triangle is a (0, 0), b (bx, 0), c (cx, cy > 0)
	const V3 e1 = (B - A) * (1.0f / Length(B - A));
	const V3 e2 = Cross(n, e1);
	const float pa[2] = { 0.0f, 0.0f };
	const float pb[2] = { Dot(B - A, e1), 0.0f };
	const float pc[2] = { Dot(C - A, e1), Dot(C - A, e2) };
	const float *corners[3] = { pa, pb, pc };

	detail::Cutter cut(m, n, A);
	const std::vector<Triangle> source = m.tris;
	std::vector<Triangle> out;
	out.reserve(source.size() + 16);
	bool changed = false;

	for (const Triangle &tri : source)
	{
		int s[3];
		for (int k = 0; k < 3; ++k)
			s[k] = cut.Side(tri.v[k]);
		if (s[0] == s[1] && s[1] == s[2])
		{
			out.push_back(tri);
			continue;
		}
		// the two crossed edges: crossing points P (edge eP) and Q (edge eQ)
		int edges[2], found = 0;
		V3 cross[2];
		for (int e = 0; e < 3 && found < 2; ++e)
		{
			const int a = tri.v[e], b = tri.v[(e + 1) % 3];
			if (s[e] == s[(e + 1) % 3])
				continue;
			const float d0 = cut.dist[a], d1 = cut.dist[b];
			float t = d0 != d1 ? d0 / (d0 - d1) : 0.5f;
			t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
			edges[found] = e;
			cross[found] = Lerp(m.world[a], m.world[b], t);
			++found;
		}
		if (found < 2)
		{
			out.push_back(tri);
			continue;
		}
		// clip P -> Q to the sweep triangle in its plane (Cyrus-Beck)
		const float p[2] = { Dot(cross[0] - A, e1), Dot(cross[0] - A, e2) };
		const float q[2] = { Dot(cross[1] - A, e1), Dot(cross[1] - A, e2) };
		const float d[2] = { q[0] - p[0], q[1] - p[1] };
		float t0 = 0.0f, t1 = 1.0f;
		for (int k = 0; k < 3 && t0 <= t1; ++k)
		{
			const float *c0 = corners[k];
			const float *c1 = corners[(k + 1) % 3];
			// inward normal of a counter-clockwise edge
			const float nx = -(c1[1] - c0[1]), ny = c1[0] - c0[0];
			const float num = nx * (p[0] - c0[0]) + ny * (p[1] - c0[1]);
			const float den = nx * d[0] + ny * d[1];
			if (fabsf(den) < 1e-12f)
			{
				if (num < 0.0f)
					t1 = -1.0f;
				continue;
			}
			const float t = -num / den;
			if (den > 0.0f)
				t0 = t > t0 ? t : t0;
			else
				t1 = t < t1 ? t : t1;
		}
		const float len = Length(cross[1] - cross[0]);
		if (t1 < t0)
		{
			out.push_back(tri);
			continue;
		}
		// touching the plane at one vertex inside the sweep: that vertex goes to
		// the positive side, the triangle gets a copy of it (else it would hold
		// the two sides together)
		if (len < 0.02f)
		{
			cut.ClipSide(tri, 1, out);
			cut.ClipSide(tri, -1, out);
			changed = true;
			continue;
		}
		if ((t1 - t0) * len < 0.02f)
		{
			out.push_back(tri);
			continue;
		}
		// slack at the ends: a fraction of the segment or 0.05 units
		const float eps = len > 1e-6f ? (0.02f > 0.05f / len ? 0.02f : 0.05f / len) : 1.0f;
		if (t0 <= eps && t1 >= 1.0f - eps)
		{
			cut.ClipSide(tri, 1, out);
			cut.ClipSide(tri, -1, out);
			changed = true;
			if (stats)
				++stats->split;
		}
		else if (t0 <= eps)
		{
			cut.Slit(tri, edges[0], Lerp(cross[0], cross[1], t1), out);
			changed = true;
			if (stats)
				++stats->slit;
		}
		else if (t1 >= 1.0f - eps)
		{
			cut.Slit(tri, edges[1], Lerp(cross[0], cross[1], t0), out);
			changed = true;
			if (stats)
				++stats->slit;
		}
		else
		{
			// a stab through the middle
			out.push_back(tri);
		}
	}
	if (!changed)
		return false;
	m.tris.swap(out);
	return true;
}

/*
=============
Components

Pieces of the mesh connected through welded vertices: the piece of every
triangle, and per piece whether an anchor vertex holds it. Returns the count.
=============
*/
inline int Components(const Mesh &m, const std::vector<char> &use, std::vector<int> &triPiece,
	std::vector<char> &anchored)
{
	std::vector<int> parent(m.nextWeld);
	for (int i = 0; i < m.nextWeld; ++i)
		parent[i] = i;
	auto find = [&](int a) {
		while (parent[a] != a)
		{
			parent[a] = parent[parent[a]];
			a = parent[a];
		}
		return a;
	};
	for (size_t t = 0; t < m.tris.size(); ++t)
	{
		if (!use.empty() && !use[t])
			continue;
		const Triangle &tri = m.tris[t];
		const int r0 = find(m.verts[tri.v[0]].weld);
		parent[find(m.verts[tri.v[1]].weld)] = r0;
		parent[find(m.verts[tri.v[2]].weld)] = r0;
	}
	std::unordered_map<int, int> pieceOf;
	triPiece.assign(m.tris.size(), -1);
	anchored.clear();
	for (size_t t = 0; t < m.tris.size(); ++t)
	{
		if (!use.empty() && !use[t])
			continue;
		const Triangle &tri = m.tris[t];
		const int root = find(m.verts[tri.v[0]].weld);
		auto it = pieceOf.find(root);
		if (it == pieceOf.end())
		{
			it = pieceOf.emplace(root, (int)anchored.size()).first;
			anchored.push_back(0);
		}
		triPiece[t] = it->second;
		for (int k = 0; k < 3; ++k)
			if (m.verts[tri.v[k]].anchor)
				anchored[it->second] = 1;
	}
	return (int)anchored.size();
}

} // namespace plantcut
