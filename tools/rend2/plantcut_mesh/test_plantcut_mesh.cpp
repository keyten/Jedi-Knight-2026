// CPU test of the plant saber cut mesh (shared/rd-rend2/tr_plantcut_mesh.h):
// a standing card (grass card / frond) with a UV seam down its middle, cut by
// blade sweeps. Build and run: tools/rend2/plantcut_mesh/run.sh
#include "../../../shared/rd-rend2/tr_plantcut_mesh.h"
#include <cstdio>
#include <cstdlib>
#include <map>

using namespace plantcut;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); ++failures; } } while (0)

// card in the x-z plane: width 32 (x -16..16), height 64 (z 0..64), cols x
// rows quads; the middle column of vertices is duplicated (UV seam), welded
static Mesh Card(int cols, int rows)
{
	Mesh m;
	std::map<std::pair<int, int>, int> weld;
	auto vertex = [&](int i, int j, int copy) {
		Vertex v = {};
		v.rest = MakeV3(-16.0f + 32.0f * i / cols, 0.0f, 64.0f * j / rows);
		v.normal = MakeV3(0.0f, -1.0f, 0.0f);
		v.st[0] = (float)i / cols + copy * 0.5f;	// the seam copy has other UVs
		v.st[1] = (float)j / rows;
		v.tangent[0] = 1.0f; v.tangent[3] = 1.0f;
		v.frond = 0;
		v.u = v.rest.z / 16.0f;
		v.anchor = j == 0;
		auto it = weld.find(std::make_pair(i, j));
		if (it == weld.end())
			it = weld.emplace(std::make_pair(i, j), m.nextWeld++).first;
		v.weld = it->second;
		m.verts.push_back(v);
		m.world.push_back(v.rest);
		return (int)m.verts.size() - 1;
	};
	std::vector<int> left((cols + 1) * (rows + 1)), right((cols + 1) * (rows + 1));
	for (int j = 0; j <= rows; ++j)
		for (int i = 0; i <= cols; ++i)
		{
			left[j * (cols + 1) + i] = vertex(i, j, 0);
			right[j * (cols + 1) + i] = i == cols / 2 ? vertex(i, j, 1) : left[j * (cols + 1) + i];
		}
	for (int j = 0; j < rows; ++j)
		for (int i = 0; i < cols; ++i)
		{
			const std::vector<int> &g = i >= cols / 2 ? right : left;
			const int a = g[j * (cols + 1) + i], b = g[j * (cols + 1) + i + 1];
			const int c = g[(j + 1) * (cols + 1) + i + 1], d = g[(j + 1) * (cols + 1) + i];
			m.tris.push_back({ { a, b, c }, 0 });
			m.tris.push_back({ { a, c, d }, 0 });
		}
	return m;
}

static float Area(const Mesh &m)
{
	float a = 0.0f;
	for (const Triangle &t : m.tris)
		a += 0.5f * Length(Cross(m.verts[t.v[1]].rest - m.verts[t.v[0]].rest, m.verts[t.v[2]].rest - m.verts[t.v[0]].rest));
	return a;
}

static int Pieces(const Mesh &m, int *unanchored)
{
	std::vector<int> triPiece;
	std::vector<char> anchored;
	const int n = Components(m, std::vector<char>(), triPiece, anchored);
	*unanchored = 0;
	for (char a : anchored)
		*unanchored += !a;
	return n;
}

// no triangle crosses the plane z = h (all vertices of each on one side)
static bool Separated(const Mesh &m, float h)
{
	for (const Triangle &t : m.tris)
	{
		bool above = false, below = false;
		for (int k = 0; k < 3; ++k)
		{
			const float z = m.verts[t.v[k]].rest.z;
			above = above || z > h + 1e-3f;
			below = below || z < h - 1e-3f;
		}
		if (above && below)
			return false;
	}
	return true;
}

// blade swept horizontally through the card at height h: base at y = -40,
// tip at y = +40, moving along x from x0 to x1 (two sweep triangles)
static bool Sweep(Mesh &m, float h, float x0, float x1, CutStats *stats)
{
	const V3 b0 = MakeV3(x0, -40.0f, h), t0 = MakeV3(x0, 40.0f, h);
	const V3 b1 = MakeV3(x1, -40.0f, h), t1 = MakeV3(x1, 40.0f, h);
	const bool a = CutByTriangle(m, b0, t0, t1, stats);
	const bool b = CutByTriangle(m, b0, t1, b1, stats);
	return a || b;
}

int main()
{
	// 1. full cut: the top comes off as one piece (across the UV seam)
	{
		Mesh m = Card(4, 8);
		const float area = Area(m);
		CutStats st;
		const bool cut = Sweep(m, 37.0f, -40.0f, 40.0f, &st);
		int loose;
		const int pieces = Pieces(m, &loose);
		CHECK(cut, "full cut did nothing");
		CHECK(pieces == 2 && loose == 1, "full cut: %d pieces, %d loose", pieces, loose);
		CHECK(fabsf(Area(m) - area) < 1e-2f, "full cut area %f vs %f", Area(m), area);
		CHECK(Separated(m, 37.0f), "full cut: a triangle still crosses");
		CHECK(st.slit == 0 && st.split > 0, "full cut stats split %d slit %d", st.split, st.slit);
		// the loose piece starts at the cut: its lowest u is 37 / 16
		float uMin = 1e9f;
		std::vector<int> tp;
		std::vector<char> an;
		Components(m, std::vector<char>(), tp, an);
		for (size_t t = 0; t < m.tris.size(); ++t)
			if (!an[tp[t]])
				for (int k = 0; k < 3; ++k)
					uMin = fminf(uMin, m.verts[m.tris[t].v[k]].u);
		CHECK(fabsf(uMin - 37.0f / 16.0f) < 1e-4f, "loose piece u min %f", uMin);
		printf("full cut: %d triangles split, %d -> %zu triangles, 2 pieces, area kept\n",
			st.split, 16 * 4, m.tris.size());
	}
	// 2. partial cut: the blade stops in the middle, the card stays one piece
	{
		Mesh m = Card(4, 8);
		const float area = Area(m);
		CutStats st;
		const bool cut = Sweep(m, 37.0f, -40.0f, 3.0f, &st);
		int loose;
		const int pieces = Pieces(m, &loose);
		CHECK(cut, "partial cut did nothing");
		CHECK(pieces == 1 && loose == 0, "partial cut: %d pieces, %d loose", pieces, loose);
		CHECK(fabsf(Area(m) - area) < 1e-2f, "partial cut area");
		CHECK(st.slit == 1, "partial cut: %d slit triangles", st.slit);
		printf("partial cut: %d split + %d slit, still one piece\n", st.split, st.slit);
		// cutting the rest of the way takes it off
		CutStats st2;
		Sweep(m, 37.0f, 2.0f, 40.0f, &st2);
		const int pieces2 = Pieces(m, &loose);
		CHECK(pieces2 == 2 && loose == 1, "second half: %d pieces, %d loose", pieces2, loose);
		CHECK(fabsf(Area(m) - area) < 1e-2f, "second half area");
		printf("rest of the cut: %d pieces, %d loose\n", pieces2, loose);
	}
	// 3. a stab: the blade sweeps a small patch inside one triangle
	{
		Mesh s = Card(1, 1);
		// inside the lower left triangle only (its diagonal is at x = -1 there)
		const V3 b0 = MakeV3(-10.0f, -1.0f, 30.0f), t0 = MakeV3(-10.0f, 1.0f, 30.0f);
		const V3 t1 = MakeV3(-6.0f, 1.0f, 30.0f);
		const bool stab = CutByTriangle(s, b0, t0, t1);
		CHECK(!stab, "stab cut something");
		printf("stab: nothing cut\n");
	}
	// 4. a cut exactly through a row of vertices (z = 32 is a grid row)
	{
		Mesh m = Card(4, 8);
		const float area = Area(m);
		Sweep(m, 32.0f, -40.0f, 40.0f, nullptr);
		int loose;
		const int pieces = Pieces(m, &loose);
		CHECK(pieces == 2 && loose == 1, "through vertices: %d pieces, %d loose", pieces, loose);
		CHECK(fabsf(Area(m) - area) < 1e-2f, "through vertices area");
		CHECK(Separated(m, 32.0f), "through vertices: crossing triangle");
		printf("cut through a vertex row: %d pieces\n", pieces);
	}
	// 5. a slanted cut and a second cut of the loose part's base: three pieces
	{
		Mesh m = Card(6, 12);
		const float area = Area(m);
		const V3 b0 = MakeV3(-40.0f, -40.0f, 20.0f), t0 = MakeV3(-40.0f, 40.0f, 20.0f);
		const V3 b1 = MakeV3(40.0f, -40.0f, 44.0f), t1 = MakeV3(40.0f, 40.0f, 44.0f);
		CutByTriangle(m, b0, t0, t1);
		CutByTriangle(m, b0, t1, b1);
		Sweep(m, 54.0f, -40.0f, 40.0f, nullptr);
		int loose;
		const int pieces = Pieces(m, &loose);
		CHECK(pieces == 3 && loose == 2, "two cuts: %d pieces, %d loose", pieces, loose);
		CHECK(fabsf(Area(m) - area) < 1e-2f, "two cuts area %f %f", Area(m), area);
		printf("slanted + level cut: %d pieces, %d loose, %zu triangles\n", pieces, loose, m.tris.size());
	}
	// 6. a deformed pose: the world positions are bent, the rest stays flat
	{
		Mesh m = Card(4, 8);
		for (size_t i = 0; i < m.verts.size(); ++i)
		{
			// bend: the top leans along +y by z^2 / 128
			const V3 r = m.verts[i].rest;
			m.world[i] = MakeV3(r.x, r.z * r.z / 128.0f, r.z);
		}
		// a level cut at world z 40: rest z is the same (the bend keeps z)
		const V3 b0 = MakeV3(-40.0f, -80.0f, 40.0f), t0 = MakeV3(-40.0f, 80.0f, 40.0f);
		const V3 b1 = MakeV3(40.0f, -80.0f, 40.0f), t1 = MakeV3(40.0f, 80.0f, 40.0f);
		CutByTriangle(m, b0, t0, t1);
		CutByTriangle(m, b0, t1, b1);
		int loose;
		const int pieces = Pieces(m, &loose);
		CHECK(pieces == 2 && loose == 1, "bent: %d pieces", pieces);
		CHECK(Separated(m, 40.0f), "bent: rest separation at the world height");
		printf("bent pose: cut lands at the same rest height, %d pieces\n", pieces);
	}

	if (failures)
	{
		printf("%d FAILED\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
