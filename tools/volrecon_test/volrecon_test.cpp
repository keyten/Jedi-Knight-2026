/*
Synthetic tests of the directional baked volumetric lighting reconstruction
(shared/rd-rend2/tr_volrecon.cpp, r_volumetricFogStaticDirectional).

Builds q3map-like light grids (inverse square / linear point lights, rectangle
emitters, a sun, walls that block light and invalidate cells, one dominant
direction per cell quantized to latLong bytes, the directed / ambient split) and
checks the cases of the design plus the invariants on every case, in modes 1 and 2,
and mode 1 again with the structured lights promoted (a box trace as the BSP):

  B + P + S == legacy (float, and after the half float packing), legacy from the inputs
  B' + P == B, B' >= 0, P >= 0; promoted lights physical, leak <= 15 %, at most 32
  the structured light classes: omni POINT, true SPOT, doorway lamp neither spot nor
  promoted, a lamp against a wall stays in the room
  wall cells: sun fraction 0, no baked sun; the static alpha packs the sun fraction
  at most 256 sources in total
  |M_c| <= B_c (float and half)
  B + 3 g M.v >= 0 for |g| <= 1/3, no NaN

Build (MSVC, from this directory):
  cl /nologo /O2 /EHsc /I..\..\shared\rd-rend2 volrecon_test.cpp ..\..\shared\rd-rend2\tr_volrecon.cpp
Run: volrecon_test.exe   (exit code 0 = all passed)
*/

#include "tr_volrecon.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

const float PI = 3.14159265358979f;
const float LUMA[3] = { 0.2126f, 0.7152f, 0.0722f };

struct V3
{
	float x, y, z;
};
V3 Make( float x, float y, float z ) { V3 v = { x, y, z }; return v; }
V3 Add( V3 a, V3 b ) { return Make(a.x + b.x, a.y + b.y, a.z + b.z); }
V3 Sub( V3 a, V3 b ) { return Make(a.x - b.x, a.y - b.y, a.z - b.z); }
V3 Scale( V3 a, float s ) { return Make(a.x * s, a.y * s, a.z * s); }
float Dot( V3 a, V3 b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Length( V3 a ) { return sqrtf(Dot(a, a)); }
V3 Normalize( V3 a ) { const float l = Length(a); return l > 1e-12f ? Scale(a, 1.0f / l) : Make(0, 0, 0); }
V3 Cross( V3 a, V3 b ) { return Make(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
float Luma( V3 c ) { return LUMA[0] * c.x + LUMA[1] * c.y + LUMA[2] * c.z; }

struct Box
{
	V3 mins, maxs;
};

struct PointLight
{
	V3 pos;
	V3 color;		// radiant intensity, rgb
	float radius;	// > 0: linear falloff to 0 at radius, else inverse square
	V3 spotDir;		// non zero: a spot light along it (unit)
	float cosOuter, cosInner;
};

struct RectLight
{
	V3 center, right, up;	// unit axes, normal = cross(right, up)
	float halfWidth, halfHeight;
	V3 radiance;
};

struct Scene
{
	int dims[3];
	V3 cellSize;
	bool hdr;
	float ambient;
	std::vector<Box> walls;
	std::vector<PointLight> points;
	std::vector<RectLight> rects;
	bool hasSun;
	V3 sunDir;
	V3 sunColor;
	std::vector<vrAreaSource> areas;	// what the renderer would detect
};

// segment a -> b intersects the box (slab test, a small shrink so that a box face the
// segment starts on does not block it)
bool SegmentHitsBox( V3 a, V3 b, const Box& box )
{
	float t0 = 1e-4f, t1 = 1.0f - 1e-4f;
	const float o[3] = { a.x, a.y, a.z }, d[3] = { b.x - a.x, b.y - a.y, b.z - a.z };
	const float mn[3] = { box.mins.x, box.mins.y, box.mins.z }, mx[3] = { box.maxs.x, box.maxs.y, box.maxs.z };
	for ( int k = 0; k < 3; k++ )
	{
		if ( fabsf(d[k]) < 1e-9f )
		{
			if ( o[k] <= mn[k] || o[k] >= mx[k] )
				return false;
			continue;
		}
		float ta = (mn[k] - o[k]) / d[k], tb = (mx[k] - o[k]) / d[k];
		if ( ta > tb ) { const float t = ta; ta = tb; tb = t; }
		t0 = std::max(t0, ta);
		t1 = std::min(t1, tb);
		if ( t0 >= t1 )
			return false;
	}
	return true;
}

bool Inside( V3 p, const Box& b )
{
	return p.x > b.mins.x && p.x < b.maxs.x && p.y > b.mins.y && p.y < b.maxs.y && p.z > b.mins.z && p.z < b.maxs.z;
}

bool Visible( const Scene& s, V3 a, V3 b )
{
	for ( const Box& w : s.walls )
		if ( SegmentHitsBox(a, b, w) )
			return false;
	return true;
}

// latLong bytes as q3map stores them, decoded as the renderer does (tr_volumetric_reconstruct.cpp)
V3 QuantizeDirection( V3 d )
{
	d = Normalize(d);
	float lng = acosf(std::max(-1.0f, std::min(1.0f, d.z)));
	float lat = atan2f(d.y, d.x);
	if ( lat < 0.0f )
		lat += 2.0f * PI;
	const int b0 = ((int)floorf(lng * 256.0f / (2.0f * PI) + 0.5f)) & 255;
	const int b1 = ((int)floorf(lat * 256.0f / (2.0f * PI) + 0.5f)) & 255;
	const float la = b1 * (2.0f * PI / 256.0f), ln = b0 * (2.0f * PI / 256.0f);
	return Make(cosf(la) * sinf(ln), sinf(la) * sinf(ln), cosf(ln));
}

float Smoothstep( float e0, float e1, float x )
{
	const float t = std::max(0.0f, std::min(1.0f, (x - e0) / (e1 - e0)));
	return t * t * (3.0f - 2.0f * t);
}

// as the renderer traces: 0 when blocked or starting in a wall
float TraceScene( void *user, const float start[3], const float end[3] )
{
	const Scene& s = *(const Scene *)user;
	const V3 a = Make(start[0], start[1], start[2]), b = Make(end[0], end[1], end[2]);
	for ( const Box& w : s.walls )
		if ( Inside(a, w) )
			return 0.0f;
	return Visible(s, a, b) ? 1.0f : 0.0f;
}

struct Grid
{
	Scene scene;
	int n;
	std::vector<float> ambient, direct, bspDir, sunAlign, sunVis;
	std::vector<uint8_t> valid;
	vrInput input;

	V3 Position( int x, int y, int z ) const
	{
		return Make(x * scene.cellSize.x, y * scene.cellSize.y, z * scene.cellSize.z);
	}
	int Index( int x, int y, int z ) const { return x + scene.dims[0] * (y + scene.dims[1] * z); }
	int IndexAt( V3 p ) const
	{
		const int x = (int)floorf(p.x / scene.cellSize.x + 0.5f);
		const int y = (int)floorf(p.y / scene.cellSize.y + 0.5f);
		const int z = (int)floorf(p.z / scene.cellSize.z + 0.5f);
		return Index(x, y, z);
	}
};

void BuildGrid( Grid& g, const Scene& s, int mode )
{
	g.scene = s;
	g.n = s.dims[0] * s.dims[1] * s.dims[2];
	g.ambient.assign(g.n * 3, 0.0f);
	g.direct.assign(g.n * 3, 0.0f);
	g.bspDir.assign(g.n * 3, 0.0f);
	g.sunAlign.assign(g.n, 0.0f);
	g.sunVis.assign(g.n, 1.0f);
	g.valid.assign(g.n, 1);

	float maxLdr = 0.0f;
	for ( int z = 0; z < s.dims[2]; z++ )
		for ( int y = 0; y < s.dims[1]; y++ )
			for ( int x = 0; x < s.dims[0]; x++ )
			{
				const int i = g.Index(x, y, z);
				const V3 p = g.Position(x, y, z);
				for ( const Box& w : s.walls )
					if ( Inside(p, w) )
						g.valid[i] = 0;

				struct Contrib { V3 color; V3 dir; };
				std::vector<Contrib> contribs;
				for ( const PointLight& l : s.points )
				{
					const V3 d = Sub(l.pos, p);
					const float r = std::max(Length(d), 16.0f);
					if ( !Visible(s, p, l.pos) )
						continue;
					float k;
					if ( l.radius > 0.0f )
						k = std::max(0.0f, 1.0f - r / l.radius) / (64.0f * 64.0f);
					else
						k = 1.0f / (r * r);
					if ( Length(l.spotDir) > 0.0f )
						k *= Smoothstep(l.cosOuter, l.cosInner, Dot(Normalize(Sub(p, l.pos)), l.spotDir));
					if ( k > 0.0f )
					{
						Contrib c = { Scale(l.color, k), Normalize(d) };
						contribs.push_back(c);
					}
				}
				for ( const RectLight& l : s.rects )
				{
					const V3 normal = Normalize(Cross(l.right, l.up));
					const int ns = 8;
					const float sampleArea = 4.0f * l.halfWidth * l.halfHeight / (ns * ns);
					V3 sum = Make(0, 0, 0), dir = Make(0, 0, 0);
					for ( int b = 0; b < ns; b++ )
						for ( int a = 0; a < ns; a++ )
						{
							const V3 sp = Add(l.center, Add(Scale(l.right, ((a + 0.5f) / ns * 2 - 1) * l.halfWidth),
								Scale(l.up, ((b + 0.5f) / ns * 2 - 1) * l.halfHeight)));
							const V3 d = Sub(sp, p);
							const float r = std::max(Length(d), 16.0f);
							const V3 u = Normalize(d);
							const float cosine = std::max(0.0f, -Dot(normal, u));
							if ( cosine <= 0.0f || !Visible(s, p, sp) )
								continue;
							const float w = sampleArea * cosine / (r * r);
							sum = Add(sum, Scale(l.radiance, w));
							dir = Add(dir, Scale(u, w));
						}
					if ( Luma(sum) > 0.0f )
					{
						Contrib c = { sum, Normalize(dir) };
						contribs.push_back(c);
					}
				}
				bool sunLit = false;
				if ( s.hasSun && Visible(s, p, Add(p, Scale(s.sunDir, 1e5f))) )
				{
					Contrib c = { s.sunColor, s.sunDir };
					contribs.push_back(c);
					sunLit = true;
				}

				// dominant direction: luminance weighted, then the directed / ambient split
				V3 main = Make(0, 0, 0);
				for ( const Contrib& c : contribs )
					main = Add(main, Scale(c.dir, Luma(c.color)));
				main = Length(main) > 1e-12f ? Normalize(main) : Make(0, 0, 1);
				main = QuantizeDirection(main);
				V3 A = Make(s.ambient, s.ambient, s.ambient), D = Make(0, 0, 0);
				for ( const Contrib& c : contribs )
				{
					const float d = std::max(0.0f, Dot(c.dir, main));
					D = Add(D, Scale(c.color, d));
					A = Add(A, Scale(c.color, 1.0f - d));
				}
				g.ambient[i * 3 + 0] = A.x; g.ambient[i * 3 + 1] = A.y; g.ambient[i * 3 + 2] = A.z;
				g.direct[i * 3 + 0] = D.x; g.direct[i * 3 + 1] = D.y; g.direct[i * 3 + 2] = D.z;
				g.bspDir[i * 3 + 0] = main.x; g.bspDir[i * 3 + 1] = main.y; g.bspDir[i * 3 + 2] = main.z;
				maxLdr = std::max(maxLdr, std::max(std::max(A.x, std::max(A.y, A.z)), std::max(D.x, std::max(D.y, D.z))));

				if ( s.hasSun )
				{
					// as the renderer: alignment with the sun, sky visibility of the center and
					// four tetrahedral corners half a cell out
					g.sunAlign[i] = Smoothstep(cosf(25.0f * PI / 180.0f), cosf(10.0f * PI / 180.0f), Dot(main, s.sunDir));
					static const float corners[5][3] = { { 0, 0, 0 }, { 1, 1, 1 }, { 1, -1, -1 }, { -1, 1, -1 }, { -1, -1, 1 } };
					int visible = 0;
					for ( int k = 0; k < 5; k++ )
					{
						const V3 q = Add(p, Make(0.5f * corners[k][0] * s.cellSize.x, 0.5f * corners[k][1] * s.cellSize.y,
							0.5f * corners[k][2] * s.cellSize.z));
						if ( Visible(s, q, Add(q, Scale(s.sunDir, 1e5f))) )
							visible++;
					}
					g.sunVis[i] = visible / 5.0f;
					(void)sunLit;
				}
			}

	// LDR: bytes of the legacy grid (scaled so that the brightest value is 1)
	if ( !s.hdr && maxLdr > 0.0f )
		for ( int k = 0; k < g.n * 3; k++ )
		{
			g.ambient[k] = floorf(g.ambient[k] / maxLdr * 255.0f + 0.5f) / 255.0f;
			g.direct[k] = floorf(g.direct[k] / maxLdr * 255.0f + 0.5f) / 255.0f;
		}

	vrInput& in = g.input;
	memset(&in, 0, sizeof(in));
	for ( int k = 0; k < 3; k++ )
		in.dims[k] = s.dims[k];
	in.origin[0] = in.origin[1] = in.origin[2] = 0.0f;
	in.cellSize[0] = s.cellSize.x; in.cellSize[1] = s.cellSize.y; in.cellSize[2] = s.cellSize.z;
	in.hdr = s.hdr;
	in.mode = mode;
	in.ambient = g.ambient.data();
	in.direct = g.direct.data();
	in.bspDir = g.bspDir.data();
	in.valid = g.valid.data();
	in.sunAlign = s.hasSun ? g.sunAlign.data() : nullptr;
	in.sunVis = s.hasSun ? g.sunVis.data() : nullptr;
	in.areas = s.areas.empty() ? nullptr : s.areas.data();
	in.numAreas = (int)s.areas.size();
	in.trace = TraceScene;
	in.traceUser = &g.scene;
	in.promote = true;
	in.entityProbes = true;
	in.portals = true;
	in.promotePortals = true;
	for ( vrAreaSource& a : g.scene.areas )
		a.sourceIndex = (int)(&a - g.scene.areas.data());
	in.areas = g.scene.areas.empty() ? nullptr : g.scene.areas.data();
}

int g_failures = 0;
int g_checks = 0;
std::string g_case;

void Check( bool ok, const char *what, double value = 0.0 )
{
	g_checks++;
	if ( !ok )
	{
		g_failures++;
		printf("  FAIL [%s] %s (%g)\n", g_case.c_str(), what, value);
	}
}

// the invariants every output must satisfy
void CheckInvariants( const Grid& g, const vrOutput& out )
{
	float maxSplit = 0.0f, maxHalfSplit = 0.0f, worstMoment = 0.0f, worstHalfMoment = 0.0f, worstPhase = 0.0f;
	bool finite = true, negative = false;
	std::vector<uint16_t> baseline, moments[3];
	VR_PackHalf(out, out.sunFraction.data(), baseline, moments);
	float worstAlpha = 0.0f;
	bool wallSun = false;
	for ( int i = 0; i < g.n; i++ )
	{
		worstAlpha = std::max(worstAlpha, fabsf(VR_HalfToFloat(baseline[i * 4 + 3]) - out.sunFraction[i]));
		if ( !g.valid[i] && (out.sunFraction[i] != 0.0f || out.sun[i * 3] != 0.0f || out.sun[i * 3 + 1] != 0.0f ||
			out.sun[i * 3 + 2] != 0.0f) )
			wallSun = true;
	}
	Check(worstAlpha <= 1e-3f, "static alpha == sun fraction (half)", worstAlpha);
	Check(!wallSun, "wall cells are never sun");
	Check(out.proxies.size() <= 256 && out.stats.pointProxies + out.stats.areaAnchors <= 256, "at most 256 sources",
		(double)out.proxies.size());
	bool proxiesSane = true;
	for ( const vrProxy& p : out.proxies )
		if ( (p.type == VR_SOURCE_RECT) != (p.area >= 0) || (p.type == VR_SOURCE_RECT && !(p.halfWidth > 0.0f)) )
			proxiesSane = false;
	Check(proxiesSane, "proxy type / rect parameters");
	const bool hasMoments = !out.moment[0].empty();

	// 26 directions
	std::vector<V3> dirs;
	for ( int z = -1; z <= 1; z++ )
		for ( int y = -1; y <= 1; y++ )
			for ( int x = -1; x <= 1; x++ )
				if ( x | y | z )
					dirs.push_back(Normalize(Make((float)x, (float)y, (float)z)));

	for ( int i = 0; i < g.n; i++ )
		for ( int c = 0; c < 3; c++ )
		{
			const float a = std::max(g.ambient[i * 3 + c], 0.0f), d = std::max(g.direct[i * 3 + c], 0.0f);
			const float L = g.scene.hdr ? a + d : std::max(a, d);
			const float B = out.baseline[i * 3 + c], S = out.sun[i * 3 + c];
			const float P = out.promoted.empty() ? 0.0f : out.promoted[i * 3 + c];
			finite = finite && std::isfinite(B) && std::isfinite(S) && std::isfinite(P);
			maxSplit = std::max(maxSplit, fabsf(B + P + S - L) / std::max(L, 1.0f));
			negative = negative || B < -1e-7f || P < 0.0f;
			const float Bh = VR_HalfToFloat(baseline[i * 4 + c]);
			const float Sh = VR_HalfToFloat(VR_FloatToHalf(S));
			if ( L > 1e-4f )
				maxHalfSplit = std::max(maxHalfSplit, fabsf(Bh + P + Sh - L) / L);
			if ( !hasMoments )
				continue;
			const float *m = &out.moment[c][i * 3];
			finite = finite && std::isfinite(m[0]) && std::isfinite(m[1]) && std::isfinite(m[2]);
			const float len = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
			worstMoment = std::max(worstMoment, len - B);
			float mh[3];
			for ( int k = 0; k < 3; k++ )
				mh[k] = VR_HalfToFloat(moments[c][i * 4 + k]);
			const float lenH = sqrtf(mh[0] * mh[0] + mh[1] * mh[1] + mh[2] * mh[2]);
			worstHalfMoment = std::max(worstHalfMoment, lenH - Bh);
			for ( const V3& v : dirs )
				for ( float gs : { -1.0f / 3.0f, 1.0f / 3.0f } )
				{
					const float value = Bh + 3.0f * gs * (mh[0] * v.x + mh[1] * v.y + mh[2] * v.z);
					worstPhase = std::min(worstPhase, value);
				}
		}
	Check(finite, "all outputs finite");
	Check(maxSplit <= 1e-6f, "B + P + S == legacy (float)", maxSplit);
	Check(maxHalfSplit <= 2e-3f, "B + P + S == legacy after half rounding (relative)", maxHalfSplit);
	Check(!negative, "B' >= 0, P >= 0");
	Check(out.stats.maxPartitionError <= 1e-5f, "B' + P == B", out.stats.maxPartitionError);
	Check(out.lights.size() == out.proxies.size(), "one structured light per proxy", (double)out.lights.size());
	int promoted = 0;
	bool lightsSane = true;
	for ( const vrStaticLight& l : out.lights )
	{
		if ( l.flags & VR_LIGHT_PROMOTED )
		{
			promoted++;
			if ( !(l.flags & VR_LIGHT_PHYSICAL) || l.kind == VR_LIGHT_TRANSPORT || l.kind == VR_LIGHT_RECT ||
				!(l.radius > 0.0f) || !(l.promotionWeight > 0.0f) || l.leakFraction > 0.15f )
				lightsSane = false;
		}
		if ( l.kind == VR_LIGHT_SPOT && !(l.cosInner > l.cosOuter) )
			lightsSane = false;
		if ( !std::isfinite(l.color[0]) || !std::isfinite(l.physicalConfidence) )
			lightsSane = false;
	}
	Check(lightsSane, "structured lights sane");
	bool dirSane = true;
	for ( int c = 0; c < 3; c++ )
		for ( size_t k = 0; k + 2 < out.entityDir[c].size(); k += 3 )
		{
			const float *v = &out.entityDir[c][k];
			const float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
			if ( !std::isfinite(len) || len > 1.0f + 1e-4f )
				dirSane = false;
		}
	Check(dirSane, "entity direction mix |v| <= 1, finite");
	Check(!g.input.entityProbes || out.entityDir[0].size() == (size_t)g.n * 3, "entity directions present");
	bool portalsSane = out.portals.size() <= 32;
	for ( const vrLightPortal& p : out.portals )
	{
		if ( !(p.halfWidth > 0.0f) || !(p.halfHeight > 0.0f) || !(p.range > 0.0f) || !std::isfinite(p.color[0]) )
			portalsSane = false;
		if ( (p.flags & VR_PORTAL_PROMOTED) && (p.leakFraction > 0.15f || p.excessFraction > 0.2f || (p.flags & VR_PORTAL_SKY)) )
			portalsSane = false;
	}
	Check(portalsSane, "portals sane");
	Check(promoted <= 32 && promoted == out.stats.promotedPoints + out.stats.promotedSpots, "promoted budget", promoted);
	Check(out.promoted.empty() == (promoted + out.stats.promotedPortals == 0), "P present iff promoted");
	Check(worstMoment <= 1e-6f, "|M_c| <= B_c (float)", worstMoment);
	Check(worstHalfMoment <= 0.0f, "|M_c| <= B_c (half)", worstHalfMoment);
	Check(worstPhase >= -1e-6f, "B + 3 g M.v >= 0 for |g| <= 1/3", worstPhase);
}

V3 Moment( const vrOutput& out, int c, int i )
{
	return Make(out.moment[c][i * 3], out.moment[c][i * 3 + 1], out.moment[c][i * 3 + 2]);
}

V3 MomentLuma( const vrOutput& out, int i )
{
	return Add(Add(Scale(Moment(out, 0, i), LUMA[0]), Scale(Moment(out, 1, i), LUMA[1])), Scale(Moment(out, 2, i), LUMA[2]));
}

float BLuma( const vrOutput& out, int i )
{
	return LUMA[0] * out.baseline[i * 3] + LUMA[1] * out.baseline[i * 3 + 1] + LUMA[2] * out.baseline[i * 3 + 2];
}

// directionality |M| / B of the luminance at a cell
float Directionality( const vrOutput& out, int i )
{
	const float b = BLuma(out, i);
	return b > 0.0f ? Length(MomentLuma(out, i)) / b : 0.0f;
}

float Cosine( V3 a, V3 b )
{
	const float la = Length(a), lb = Length(b);
	return (la > 0.0f && lb > 0.0f) ? Dot(a, b) / (la * lb) : 0.0f;
}

const vrProxy *NearestProxy( const vrOutput& out, V3 p, float *distance )
{
	const vrProxy *best = nullptr;
	*distance = 1e30f;
	for ( const vrProxy& x : out.proxies )
	{
		const float d = Length(Sub(Make(x.position[0], x.position[1], x.position[2]), p));
		if ( d < *distance )
		{
			*distance = d;
			best = &x;
		}
	}
	return best;
}

vrOutput g_lit;

const vrStaticLight *NearestLight( const vrOutput& out, V3 p, float *distance )
{
	const vrStaticLight *best = nullptr;
	*distance = 1e30f;
	for ( const vrStaticLight& x : out.lights )
	{
		const float d = Length(Sub(Make(x.origin[0], x.origin[1], x.origin[2]), p));
		if ( d < *distance )
		{
			*distance = d;
			best = &x;
		}
	}
	return best;
}

int CountKind( const vrOutput& out, vrLightKind kind )
{
	int n = 0;
	for ( const vrStaticLight& l : out.lights )
		n += l.kind == kind ? 1 : 0;
	return n;
}

void Run( const char *name, const Scene& scene, vrOutput& out1, Grid& grid )
{
	g_case = std::string(name) + " mode 2";
	Grid g2;
	BuildGrid(g2, scene, 2);
	vrOutput out2;
	VR_Reconstruct(g2.input, out2);
	CheckInvariants(g2, out2);

	g_case = std::string(name) + " mode 1";
	BuildGrid(grid, scene, 1);
	grid.input.promote = false;
	grid.input.promotePortals = false;
	VR_Reconstruct(grid.input, out1);
	CheckInvariants(grid, out1);

	// structured lights promoted: the moments of out1 stay those of the whole baked light
	g_case = std::string(name) + " mode 1 promoted";
	grid.input.promote = true;
	grid.input.promotePortals = true;
	VR_Reconstruct(grid.input, g_lit);
	CheckInvariants(grid, g_lit);
	grid.input.promote = false;
	grid.input.promotePortals = false;
	const vrStats& st = out1.stats;
	printf("%-28s seeds %3d fits %3d points %2d areas %d dirCells %5d dir %.3f attr %.3f rms %.2f sigma %.2f  %.1f ms\n",
		name, st.seeds, st.fits, st.pointProxies, st.areaAnchors, st.directionalCells, st.directionalFraction,
		st.attributedFraction, st.meanRayRms, st.meanSigmaP, st.msecTotal);
	const vrStats& ls = g_lit.stats;
	if ( ls.portals || ls.portalCandidates )
		printf("  portals: %d (%d candidates, %d promoted, %d sky), %.1f%% of B, %d traces\n", ls.portals,
			ls.portalCandidates, ls.promotedPortals, ls.skyPortals, ls.portalFraction * 100.0f, ls.portalTraces);
	if ( getenv("VR_VERBOSE") )
		for ( const vrLightPortal& p : g_lit.portals )
			printf("    portal%s at %.0f %.0f %.0f n %.2f %.2f %.2f size %.0f x %.0f range %.0f color %.4f conf %.2f solid %.2f leak %.2f excess %.2f recv %d\n",
				(p.flags & VR_PORTAL_PROMOTED) ? " promoted" : "", p.center[0], p.center[1], p.center[2],
				p.normal[0], p.normal[1], p.normal[2], 2.0f * p.halfWidth, 2.0f * p.halfHeight, p.range,
				p.color[1], p.confidence, p.solidBorder, p.leakFraction, p.excessFraction, p.receivers);
	if ( ls.physicalLights || ls.transportLights )
		printf("  lights: physical %d transport %d relocated %d promoted %d points %d spots, %.1f%% of B, excess %.1f%%, %d traces\n",
			ls.physicalLights, ls.transportLights, ls.relocatedLights, ls.promotedPoints, ls.promotedSpots,
			ls.promotedFraction * 100.0f, ls.excessFraction * 100.0f, ls.traces);
	if ( getenv("VR_VERBOSE") )
		for ( const vrStaticLight& l : g_lit.lights )
			printf("    light %s%s%s at %.0f %.0f %.0f R %.0f color %.3f %.3f %.3f conf %.2f vis %.2f phys %.2f fit %.2f spot %.2f w %.2f leak %.2f\n",
				l.kind == VR_LIGHT_SPOT ? "spot" : l.kind == VR_LIGHT_POINT ? "point" : l.kind == VR_LIGHT_RECT ? "rect" : "transport",
				(l.flags & VR_LIGHT_PROMOTED) ? " promoted" : "", (l.flags & VR_LIGHT_RELOCATED) ? " relocated" : "",
				l.origin[0], l.origin[1], l.origin[2], l.radius, l.color[0], l.color[1], l.color[2], l.confidence,
				l.visibility, l.physicalConfidence, l.radiometricConfidence, l.spotConfidence, l.promotionWeight, l.leakFraction);
	if ( getenv("VR_VERBOSE") )
		for ( const vrProxy& p : out1.proxies )
			printf("    proxy %s at %.0f %.0f %.0f color %.2f %.2f %.2f conf %.2f sigma %.0f range %.0f support %d\n",
				p.type == VR_SOURCE_RECT ? "rect" : "point", p.position[0],
 p.position[1], p.position[2], p.color[0], p.color[1], p.color[2],
				p.confidence, p.sigmaP, p.range, p.support);
}

Scene BaseScene( int bx, int by, int bz )
{
	Scene s;
	s.dims[0] = bx; s.dims[1] = by; s.dims[2] = bz;
	s.cellSize = Make(64.0f, 64.0f, 128.0f);
	s.hdr = true;
	s.ambient = 0.002f;
	s.hasSun = false;
	s.sunDir = Make(0, 0, 1);
	s.sunColor = Make(0, 0, 0);
	return s;
}

V3 CellPos( const Scene& s, float x, float y, float z )
{
	return Make(x * s.cellSize.x, y * s.cellSize.y, z * s.cellSize.z);
}

} // namespace

int main()
{
	// one point light
	{
		Scene s = BaseScene(24, 24, 12);
		const V3 light = CellPos(s, 12.0f, 12.0f, 6.0f);
		s.points.push_back({ light, Make(4000, 3600, 3000), 0.0f });
		Grid g; vrOutput out;
		Run("one point light", s, out, g);
		float d;
		const vrProxy *p = NearestProxy(out, light, &d);
		const float diag = Length(s.cellSize);
		Check(p != nullptr && d < 0.5f * diag, "proxy near the light (cell diagonals)", d / diag);
		Check(p && p->confidence > 0.4f, "high confidence", p ? p->confidence : 0.0f);
		const int probe = g.Index(18, 12, 6);
		Check(Cosine(MomentLuma(out, probe), Sub(light, g.Position(18, 12, 6))) > 0.95f, "moment towards the light",
			Cosine(MomentLuma(out, probe), Sub(light, g.Position(18, 12, 6))));
		Check(Directionality(out, probe) > 0.3f, "directional far field", Directionality(out, probe));
		const vrStaticLight *l = NearestLight(g_lit, light, &d);
		Check(l && l->kind == VR_LIGHT_POINT && (l->flags & VR_LIGHT_PROMOTED), "omni: a promoted POINT", l ? l->kind : -1);
		Check(l && d < 0.5f * diag, "omni: light at the lamp", d / diag);
		Check(CountKind(g_lit, VR_LIGHT_SPOT) == 0, "omni: no spot", CountKind(g_lit, VR_LIGHT_SPOT));
		Check(g_lit.stats.promotedFraction > 0.05f, "omni: energy promoted", g_lit.stats.promotedFraction);
		Check(g_lit.stats.excessFraction < 0.25f, "omni: model within the budget", g_lit.stats.excessFraction);
		Check(g_lit.portals.empty(), "omni in an empty room: no portal", (double)g_lit.portals.size());
	}

	// a light between cells, smaller than the grid spacing
	{
		Scene s = BaseScene(24, 24, 12);
		const V3 light = CellPos(s, 11.37f, 12.61f, 5.73f);
		s.points.push_back({ light, Make(3000, 3000, 3000), 0.0f });
		Grid g; vrOutput out;
		Run("sub-cell light", s, out, g);
		float d;
		const vrProxy *p = NearestProxy(out, light, &d);
		const float diag = Length(s.cellSize);
		Check(p != nullptr && d < 0.5f * diag, "proxy between the cells", d / diag);
		const int nearCell = g.IndexAt(light);
		const int farCell = g.Index(17, 13, 6);
		Check(Directionality(out, farCell) > 0.3f, "far field keeps its direction", Directionality(out, farCell));
		Check(Directionality(out, nearCell) < Directionality(out, farCell), "near field less directional",
			Directionality(out, nearCell));
	}

	// red left, blue right: different RGB directions, not one purple average
	{
		Scene s = BaseScene(32, 16, 8);
		const V3 red = CellPos(s, 6.0f, 8.0f, 4.0f), blue = CellPos(s, 26.0f, 8.0f, 4.0f);
		s.points.push_back({ red, Make(6000, 200, 200), 0.0f });
		s.points.push_back({ blue, Make(200, 200, 6000), 0.0f });
		Grid g; vrOutput out;
		Run("red left + blue right", s, out, g);
		// the midpoint: q3map put the weaker (blue) lamp in the ambient part, which stays
		// isotropic (conservative budget); only the leaked blue of the red lamp may be directed
		const int probe = g.Index(16, 8, 4);
		const float cr = Cosine(Moment(out, 0, probe), Make(-1, 0, 0));
		Check(cr > 0.7f, "M_R points to the red light", cr);
		const float blueDir = Length(Moment(out, 2, probe)) / out.baseline[probe * 3 + 2];
		Check(blueDir < 0.1f, "midpoint blue not dragged along the red direction", blueDir);
		const int probeL = g.Index(11, 8, 4), probeR = g.Index(21, 8, 4);
		Check(Cosine(Moment(out, 0, probeL), Make(-1, 0, 0)) > 0.9f, "M_R left of the centre",
			Cosine(Moment(out, 0, probeL), Make(-1, 0, 0)));
		Check(Cosine(Moment(out, 2, probeR), Make(1, 0, 0)) > 0.9f, "M_B right of the centre",
			Cosine(Moment(out, 2, probeR), Make(1, 0, 0)));
		// entity L1: red from the left, blue from the right at the same probes
		const float *er = &g_lit.entityDir[0][probeL * 3], *eb = &g_lit.entityDir[2][probeR * 3];
		Check(er[0] < -0.5f, "entity L1: red from the left", er[0]);
		Check(eb[0] > 0.5f, "entity L1: blue from the right", eb[0]);
	}

	// two equal white lights opposite each other: the moments cancel in the middle
	{
		Scene s = BaseScene(32, 16, 8);
		s.points.push_back({ CellPos(s, 6.0f, 8.0f, 4.0f), Make(5000, 5000, 5000), 0.0f });
		s.points.push_back({ CellPos(s, 26.0f, 8.0f, 4.0f), Make(5000, 5000, 5000), 0.0f });
		Grid g; vrOutput out;
		Run("opposing white lights", s, out, g);
		const int probe = g.Index(16, 8, 4);
		Check(Directionality(out, probe) < 0.15f, "centre nearly isotropic", Directionality(out, probe));
		Check(out.stats.pointProxies >= 2, "two proxies", out.stats.pointProxies);
	}

	// two white lights on the same side: the moments reinforce
	{
		Scene s = BaseScene(32, 16, 8);
		s.points.push_back({ CellPos(s, 6.0f, 5.0f, 4.0f), Make(4000, 4000, 4000), 0.0f });
		s.points.push_back({ CellPos(s, 6.0f, 11.0f, 4.0f), Make(4000, 4000, 4000), 0.0f });
		Grid g; vrOutput out;
		Run("same side white lights", s, out, g);
		const int probe = g.Index(16, 8, 4);
		Check(Cosine(MomentLuma(out, probe), Make(-1, 0, 0)) > 0.9f, "broad direction towards both",
			Cosine(MomentLuma(out, probe), Make(-1, 0, 0)));
		Check(Directionality(out, probe) > 0.2f, "directional", Directionality(out, probe));
	}

	// doorway: lamp behind a wall, the room lit through an opening
	{
		Scene s = BaseScene(32, 24, 8);
		const float wx = 12.5f * s.cellSize.x;		// wall between cell 12 and 13, 0.6 cell thick
		const float t = 0.3f * s.cellSize.x;
		const float y0 = 10.5f * s.cellSize.y, y1 = 13.5f * s.cellSize.y;	// opening y 10.5 .. 13.5, z 0 .. 5.5
		const float zTop = 5.5f * s.cellSize.z, zMax = 8.0f * s.cellSize.z, yMax = 24.0f * s.cellSize.y;
		s.walls.push_back({ Make(wx - t, -100, -100), Make(wx + t, y0, zMax) });
		s.walls.push_back({ Make(wx - t, y1, -100), Make(wx + t, yMax, zMax) });
		s.walls.push_back({ Make(wx - t, y0, zTop), Make(wx + t, y1, zMax) });
		const V3 lamp = CellPos(s, 5.0f, 12.0f, 3.0f);
		s.points.push_back({ lamp, Make(8000, 7000, 6000), 0.0f });
		Grid g; vrOutput out;
		Run("doorway", s, out, g);
		const V3 opening = Make(wx, 12.0f * s.cellSize.y, 2.5f * s.cellSize.z);
		const int probe = g.Index(18, 12, 2);
		const float c = Cosine(MomentLuma(out, probe), Sub(opening, g.Position(18, 12, 2)));
		Check(c > 0.8f, "light enters from the opening", c);
		Check(Directionality(out, probe) > 0.15f, "room side keeps a direction", Directionality(out, probe));
		// the cone through the opening is occlusion, not a spot
		Check(CountKind(g_lit, VR_LIGHT_SPOT) == 0, "doorway: no spot", CountKind(g_lit, VR_LIGHT_SPOT));
		// unshadowed at runtime, its light would pass the wall: it stays baked
		Check(g_lit.stats.promotedPoints + g_lit.stats.promotedSpots == 0, "doorway: lamp behind the wall not promoted",
			g_lit.stats.promotedPoints + g_lit.stats.promotedSpots);
	}

	// doorway with a floor and a frame: the light of the lamp comes through it
	{
		Scene s = BaseScene(32, 24, 8);
		const float wx = 12.5f * s.cellSize.x, t = 0.3f * s.cellSize.x;
		const float y0 = 10.5f * s.cellSize.y, y1 = 13.5f * s.cellSize.y;
		const float zTop = 4.5f * s.cellSize.z, zMax = 9.0f * s.cellSize.z, yMax = 25.0f * s.cellSize.y;
		s.walls.push_back({ Make(-1000, -1000, -400), Make(1e4f, 1e4f, -40) });		// floor
		s.walls.push_back({ Make(wx - t, -100, -100), Make(wx + t, y0, zMax) });
		s.walls.push_back({ Make(wx - t, y1, -100), Make(wx + t, yMax, zMax) });
		s.walls.push_back({ Make(wx - t, y0, zTop), Make(wx + t, y1, zMax) });
		const V3 lamp = CellPos(s, 5.0f, 12.0f, 3.0f);
		s.points.push_back({ lamp, Make(8000, 7000, 6000), 0.0f });
		Grid g; vrOutput out;
		Run("portal doorway", s, out, g);
		const V3 opening = Make(wx, 12.0f * s.cellSize.y, 0.5f * zTop - 20.0f);
		const vrLightPortal *best = nullptr;
		float bestD = 1e30f;
		for ( const vrLightPortal& p : g_lit.portals )
		{
			const float d = Length(Sub(Make(p.center[0], p.center[1], p.center[2]), opening));
			if ( d < bestD )
			{
				bestD = d;
				best = &p;
			}
		}
		Check(best != nullptr, "doorway portal found", (double)g_lit.portals.size());
		Check(best && fabsf(best->center[0] - wx) < 0.5f * s.cellSize.x, "doorway portal in the wall plane",
			best ? best->center[0] - wx : 0.0f);
		Check(best && fabsf(best->center[1] - opening.y) < 0.75f * s.cellSize.y, "doorway portal centred on the gap",
			best ? best->center[1] - opening.y : 0.0f);
		const float narrow = best ? 2.0f * std::min(best->halfWidth, best->halfHeight) : 0.0f;
		Check(best && narrow > 0.5f * (y1 - y0) && narrow < 1.6f * (y1 - y0), "doorway portal width", narrow);
		Check(best && fabsf(best->normal[0]) > 0.7f, "doorway portal faces along x", best ? best->normal[0] : 0.0f);
	}

	// a true spot light pointing down in an open room
	{
		Scene s = BaseScene(24, 24, 12);
		const V3 lamp = CellPos(s, 12.0f, 12.0f, 10.0f);
		PointLight l = { lamp, Make(20000, 19000, 17000), 0.0f };
		l.spotDir = Make(0, 0, -1);
		l.cosOuter = cosf(35.0f * PI / 180.0f);
		l.cosInner = cosf(22.0f * PI / 180.0f);
		s.points.push_back(l);
		Grid g; vrOutput out;
		Run("true spot light", s, out, g);
		float d;
		const vrStaticLight *x = NearestLight(g_lit, lamp, &d);
		const float diag = Length(s.cellSize);
		Check(x && x->kind == VR_LIGHT_SPOT, "spot: classified SPOT", x ? x->kind : -1);
		Check(x && d < 1.0f * diag, "spot: at the lamp", d / diag);
		Check(x && x->axis[2] < -0.9f, "spot: axis down", x ? x->axis[2] : 0.0f);
		Check(x && x->cosOuter > cosf(55.0f * PI / 180.0f) && x->cosOuter < cosf(20.0f * PI / 180.0f), "spot: outer angle",
			x ? acosf(x->cosOuter) * 180.0f / PI : 0.0f);
		Check(x && (x->flags & VR_LIGHT_PROMOTED) && x->excessFraction <= 0.2f, "spot: promoted within the budget",
			x ? x->excessFraction : -1.0f);
		Check(g_lit.portals.empty(), "spot in an empty room: no portal", (double)g_lit.portals.size());
	}

	// a lamp a few units off a wall: the fit may land in the wall, the light moves out within sigmaP
	{
		Scene s = BaseScene(24, 24, 12);
		const float wx = 16.0f * s.cellSize.x + 20.0f;
		s.walls.push_back({ Make(wx, -100, -100), Make(wx + 0.4f * s.cellSize.x, 1e4f, 1e4f) });
		const V3 lamp = Make(wx - 6.0f, 12.0f * s.cellSize.y, 6.0f * s.cellSize.z);
		s.points.push_back({ lamp, Make(6000, 6000, 6000), 0.0f });
		Grid g; vrOutput out;
		Run("lamp against a wall", s, out, g);
		float d;
		const vrStaticLight *x = NearestLight(g_lit, lamp, &d);
		const float diag = Length(s.cellSize);
		Check(x && (x->flags & VR_LIGHT_PHYSICAL), "wall lamp: physical", x ? x->physicalConfidence : 0.0f);
		Check(x && x->origin[0] < wx, "wall lamp: in the room", x ? x->origin[0] - wx : 0.0f);
		Check(x && d < 1.0f * diag, "wall lamp: near the lamp", d / diag);
	}

	// large emissive ceiling panel: taken from the area detector, not many points
	{
		Scene s = BaseScene(24, 24, 10);
		RectLight panel;
		panel.center = CellPos(s, 12.0f, 12.0f, 8.3f);
		panel.right = Make(1, 0, 0);
		panel.up = Make(0, -1, 0);		// normal = cross(right, up) = (0, 0, -1): emits down
		panel.halfWidth = 3.0f * s.cellSize.x;
		panel.halfHeight = 2.0f * s.cellSize.y;
		panel.radiance = Make(3.0f, 3.0f, 3.2f);
		s.rects.push_back(panel);
		vrAreaSource a;
		memset(&a, 0, sizeof(a));
		a.center[0] = panel.center.x; a.center[1] = panel.center.y; a.center[2] = panel.center.z;
		a.right[0] = 1; a.up[1] = -1;
		a.halfWidth = panel.halfWidth; a.halfHeight = panel.halfHeight;
		a.color[0] = 1.0f; a.color[1] = 1.0f; a.color[2] = 1.05f;
		a.confidence = 0.8f;
		s.areas.push_back(a);
		Grid g; vrOutput out;
		Run("large ceiling panel", s, out, g);
		Check(out.stats.areaAnchors >= 1, "area anchor accepted", out.stats.areaAnchors);
		Check(out.stats.pointProxies <= 1, "not many point proxies", out.stats.pointProxies);
		const int nearCell = g.Index(12, 12, 7), farCell = g.Index(12, 12, 2);
		Check(Cosine(MomentLuma(out, farCell), Make(0, 0, 1)) > 0.8f, "light from above",
			Cosine(MomentLuma(out, farCell), Make(0, 0, 1)));
		Check(Directionality(out, nearCell) < Directionality(out, farCell), "shorter moment close to the panel",
			Directionality(out, nearCell));
		const vrStaticLight *rect = nullptr;
		for ( const vrStaticLight& l : g_lit.lights )
			if ( l.kind == VR_LIGHT_RECT )
				rect = &l;
		Check(rect && rect->areaSourceIndex == 0, "calibration: area index carried", rect ? rect->areaSourceIndex : -1);
		const float radiance = rect ? LUMA[0] * rect->color[0] + LUMA[1] * rect->color[1] + LUMA[2] * rect->color[2] : 0.0f;
		printf("  calibrated panel radiance %.3f (true 3.0)\n", radiance);
		Check(radiance > 1.5f && radiance < 6.0f, "calibration: panel radiance within 2x", radiance);
	}

	// two-sided panel set in a wall between two rooms: both rooms are lit by it
	{
		Scene s = BaseScene(26, 24, 8);
		const float wx = 12.5f * s.cellSize.x, t = 0.3f * s.cellSize.x;
		s.walls.push_back({ Make(wx - t, -100, -100), Make(wx + t, 1e4f, 1e4f) });
		const V3 c = CellPos(s, 12.5f, 12.0f, 3.5f);
		for ( int side = 0; side < 2; side++ )
		{
			RectLight panel;
			const float sign = side ? 1.0f : -1.0f;
			panel.center = Add(c, Make(sign * (t + 1.0f), 0, 0));
			// normal = cross(right, up) = sign * x
			panel.right = Make(0, sign, 0);
			panel.up = Make(0, 0, 1);
			panel.halfWidth = 1.5f * s.cellSize.y;
			panel.halfHeight = 1.0f * s.cellSize.z;
			panel.radiance = Make(3.0f, 3.0f, 3.0f);
			s.rects.push_back(panel);
		}
		vrAreaSource a;
		memset(&a, 0, sizeof(a));
		a.center[0] = c.x; a.center[1] = c.y; a.center[2] = c.z;
		a.right[1] = 1; a.up[2] = 1;
		a.halfWidth = 1.5f * s.cellSize.y; a.halfHeight = 1.0f * s.cellSize.z;
		a.color[0] = a.color[1] = a.color[2] = 1.0f;
		a.confidence = 0.8f;
		a.twoSided = true;
		s.areas.push_back(a);
		Grid g; vrOutput out;
		Run("two-sided panel in a wall", s, out, g);
		Check(out.stats.areaAnchors >= 1, "area anchor accepted", out.stats.areaAnchors);
		const int left = g.Index(8, 12, 3), right = g.Index(17, 12, 3);
		const float cl = Cosine(MomentLuma(out, left), Make(1, 0, 0)), cr = Cosine(MomentLuma(out, right), Make(-1, 0, 0));
		Check(cl > 0.8f, "front room: towards the panel", cl);
		Check(cr > 0.8f, "back room: towards the panel", cr);
		Check(Directionality(out, right) > 0.1f, "back room directional", Directionality(out, right));
	}

	// more static emitters than the source budget: one global cap
	{
		Scene s = BaseScene(40, 40, 4);
		s.ambient = 0.001f;
		for ( int y = 0; y < 15; y++ )
			for ( int x = 0; x < 20; x++ )
			{
				vrAreaSource a;
				memset(&a, 0, sizeof(a));
				const V3 c = CellPos(s, 1.0f + x * 2.0f, 1.0f + y * 2.5f, 2.6f);
				a.center[0] = c.x; a.center[1] = c.y; a.center[2] = c.z;
				a.right[0] = 1; a.up[1] = -1;	// emits down
				a.halfWidth = a.halfHeight = 16.0f;
				a.color[0] = a.color[1] = a.color[2] = 1.0f;
				a.confidence = 1.0f;
				s.areas.push_back(a);
				s.points.push_back({ Sub(c, Make(0, 0, 8.0f)), Make(300, 300, 300), 0.0f });
			}
		Grid g; vrOutput out;
		Run("300 emitters, source budget", s, out, g);
		printf("  anchors %d points %d dropped %d\n", out.stats.areaAnchors, out.stats.pointProxies, out.stats.droppedSources);
		Check(out.stats.areaAnchors + out.stats.pointProxies <= 256, "global budget", out.stats.areaAnchors + out.stats.pointProxies);
	}

	// outdoor sun: the explicit sun layer, no point proxies
	{
		Scene s = BaseScene(16, 16, 8);
		s.hasSun = true;
		s.sunDir = Normalize(Make(0.3f, 0.2f, 0.9f));
		s.sunColor = Make(0.9f, 0.85f, 0.7f);
		s.ambient = 0.05f;
		Grid g; vrOutput out;
		Run("outdoor sun", s, out, g);
		const int probe = g.Index(8, 8, 4);
		Check(out.sunFraction[probe] > 0.9f, "sun fraction", out.sunFraction[probe]);
		Check(out.sun[probe * 3 + 1] > 0.8f * 0.85f, "sun layer holds the sun", out.sun[probe * 3 + 1]);
		Check(out.stats.pointProxies == 0, "no point proxies", out.stats.pointProxies);
	}

	// window edge: partly open cells keep f = align * vis < 1 (the shader scales the realtime sun by it)
	{
		Scene s = BaseScene(16, 16, 8);
		s.hasSun = true;
		s.sunDir = Make(0, 0, 1);
		s.sunColor = Make(1, 1, 1);
		const float roof = 6.5f * s.cellSize.z;
		// a roof with a hole over x 6.5 .. 9.5
		s.walls.push_back({ Make(-1000, -1000, roof), Make(6.3f * s.cellSize.x, 1e4f, roof + 64.0f) });
		s.walls.push_back({ Make(9.7f * s.cellSize.x, -1000, roof), Make(1e4f, 1e4f, roof + 64.0f) });
		Grid g; vrOutput out;
		Run("window edge", s, out, g);
		bool partial = false;
		for ( int i = 0; i < g.n; i++ )
			if ( out.sunFraction[i] > 0.05f && out.sunFraction[i] < 0.95f )
				partial = true;
		Check(partial, "some cells partly in the sun");
	}

	// indoor lamp aligned with the sun: sky visibility keeps it out of the sun layer
	{
		Scene s = BaseScene(20, 20, 10);
		s.hasSun = true;
		s.sunDir = Make(0, 0, 1);
		s.sunColor = Make(1, 1, 1);
		const float roof = 8.6f * s.cellSize.z;
		s.walls.push_back({ Make(-1000, -1000, roof), Make(1e4f, 1e4f, roof + 64.0f) });	// ceiling, no sky
		const V3 lamp = CellPos(s, 10.0f, 10.0f, 8.0f);
		s.points.push_back({ lamp, Make(6000, 6000, 5000), 0.0f });
		Grid g; vrOutput out;
		Run("indoor lamp along the sun", s, out, g);
		const int probe = g.Index(10, 10, 4);
		Check(out.sunFraction[probe] < 1e-6f, "not in the sun layer", out.sunFraction[probe]);
		Check(out.sun[probe * 3 + 1] < 1e-6f, "no baked sun", out.sun[probe * 3 + 1]);
		Check(Cosine(MomentLuma(out, probe), Make(0, 0, 1)) > 0.9f, "lamp reconstructed from above",
			Cosine(MomentLuma(out, probe), Make(0, 0, 1)));
	}

	// diffuse room: no source, mostly isotropic
	{
		Scene s = BaseScene(16, 16, 8);
		s.ambient = 0.3f;
		// a very large weak panel all around: low gradients
		RectLight wash;
		wash.center = CellPos(s, 8.0f, 8.0f, 60.0f);
		wash.right = Make(1, 0, 0);
		wash.up = Make(0, -1, 0);
		wash.halfWidth = 200.0f * s.cellSize.x;
		wash.halfHeight = 200.0f * s.cellSize.y;
		wash.radiance = Make(0.02f, 0.02f, 0.02f);
		s.rects.push_back(wash);
		Grid g; vrOutput out;
		Run("diffuse room", s, out, g);
		Check(out.stats.directionalFraction < 0.05f, "mostly isotropic", out.stats.directionalFraction);
	}

	// LDR grid (byte quantized, max(A, D) legacy) with two coloured lights
	{
		Scene s = BaseScene(24, 16, 8);
		s.hdr = false;
		s.ambient = 0.0005f;
		s.points.push_back({ CellPos(s, 5.0f, 8.0f, 4.0f), Make(900, 150, 100), 0.0f });
		s.points.push_back({ CellPos(s, 19.0f, 8.0f, 4.0f), Make(100, 300, 900), 700.0f });
		Grid g; vrOutput out;
		Run("LDR coloured lights", s, out, g);
		const int probe = g.Index(8, 8, 4);
		Check(Cosine(Moment(out, 0, probe), Make(-1, 0, 0)) > 0.7f, "LDR: M_R towards the red light",
			Cosine(Moment(out, 0, probe), Make(-1, 0, 0)));
	}

	// timing on a map sized grid with many lights and some walls
	{
		Scene s = BaseScene(64, 64, 32);
		unsigned seed = 12345;
		auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
		for ( int k = 0; k < 40; k++ )
		{
			const V3 c = Make(0.3f + 0.7f * rnd(), 0.3f + 0.7f * rnd(), 0.3f + 0.7f * rnd());
			s.points.push_back({ CellPos(s, 2 + 60 * rnd(), 2 + 60 * rnd(), 2 + 28 * rnd()), Scale(c, 3000.0f + 5000.0f * rnd()),
				rnd() < 0.3f ? 600.0f : 0.0f });
		}
		for ( int k = 0; k < 12; k++ )
		{
			const V3 a = CellPos(s, 64 * rnd(), 64 * rnd(), 0.0f);
			const bool alongX = rnd() < 0.5f;
			s.walls.push_back({ a, Add(a, alongX ? Make(16 * 64.0f, 24.0f, 32 * 128.0f) : Make(24.0f, 16 * 64.0f, 32 * 128.0f)) });
		}
		Grid g; vrOutput out;
		Run("timing 64x64x32, 40 lights", s, out, g);
		const vrStats& st = out.stats;
		printf("  stages ms: split %.1f gradients %.1f seeds %.1f fit %.1f areas %.1f attribution %.1f total %.1f\n",
			st.msecSplit, st.msecGradients, st.msecSeeds, st.msecFit, st.msecAreas, st.msecAttribution, st.msecTotal);
	}

	printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
