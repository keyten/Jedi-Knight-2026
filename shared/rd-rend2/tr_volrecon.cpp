/*
===========================================================================
Copyright (C) 2026 OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

/*
Directional baked volumetric lighting reconstruction, see tr_volrecon.h.

Stages (mode 1; mode 2 stops after the split with M_c = Q_c * bspDir):

  split        S = clamp(alignment * visibility * D, 0, legacy), B = legacy - S,
               directional budget Q (HDR (1 - f) D, LDR (1 - f) max(D - A, 0), <= B)
  gradients    six-neighbour finite differences of B.rgb and its luminance, never
               through wall cells
  seeds        local maxima of Q (per channel and luminance), favoured where the
               BSP direction field and the gradient field converge
  fit          per seed: support probes flood-connected within 4 cells, BSP direction
               rays and gradient rays as separate observations, closed form least
               squares point p = A^-1 b with Huber IRLS; confidence from the ray
               residual, gradient consistency, a monotonic radial profile and the
               chromaticity stability; positional uncertainty sigma_p from the residual
               and the conditioning of A
  areas        known static emitters: quadrature moment per probe, calibrated against
               the grid (the anchor never adds its own radiance)
  attribution  per source, flood its range; per cell and channel a match score m in
               [0, 1]; directional energy E = Q * max m (weak candidates never sum up to
               the whole budget), split between the sources by m, accumulated as the
               first moment
*/

#include "tr_volrecon.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace
{

// thresholds, in cell diagonals or relative to the local brightness
const int		SEED_MAX = 512;
const int		PROXY_MAX = 256;
const int		SUPPORT_MAX = 256;
const float		SUPPORT_RADIUS = 4.0f;		// grid-normalized cells
const float		SEED_MIN_RELATIVE = 0.03f;	// of the brightest cell of the channel
const float		GRADIENT_MIN_STRENGTH = 0.05f;
const float		HUBER_DELTA = 0.5f;			// cell diagonals
const float		SIGMA_RAY = 0.75f;			// cell diagonals
const float		CONDITION_THRESHOLD = 0.1f;	// lambda_min / lambda_max of a well localized fit
const float		MIN_SOURCE_CONFIDENCE = 0.2f;
const float		MERGE_DISTANCE = 0.75f;		// cell diagonals
const float		MERGE_DISTANCE_MAX = 1.5f;	// cell diagonals, with the positional uncertainty
const float		MERGE_CHROMA_COS = 0.95f;
const float		AREA_SUPPORT_RADIUS = 3.0f;	// grid-normalized cells
const float		RANGE_MIN = 2.0f;			// cell diagonals
const float		RANGE_MAX = 10.0f;			// cell diagonals
const float		RANGE_FALLOFF = 0.01f;		// range: predicted light down to 1 % of the mean baked light
const int		DOMAIN_MAX = 32768;			// cells attributed to one source
const int		PROFILE_BINS = 10;			// of half a cell diagonal

const float		LUMA[3] = { 0.2126f, 0.7152f, 0.0722f };

typedef std::chrono::steady_clock Clock;

float Msec( Clock::time_point start )
{
	return std::chrono::duration<float, std::milli>(Clock::now() - start).count();
}

struct V3
{
	float x, y, z;
};

inline V3 Make( float x, float y, float z ) { V3 v = { x, y, z }; return v; }
inline V3 Load( const float *p ) { return Make(p[0], p[1], p[2]); }
inline V3 Add( V3 a, V3 b ) { return Make(a.x + b.x, a.y + b.y, a.z + b.z); }
inline V3 Sub( V3 a, V3 b ) { return Make(a.x - b.x, a.y - b.y, a.z - b.z); }
inline V3 Scale( V3 a, float s ) { return Make(a.x * s, a.y * s, a.z * s); }
inline float Dot( V3 a, V3 b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Length( V3 a ) { return sqrtf(Dot(a, a)); }
inline float Get( V3 a, int k ) { return k == 0 ? a.x : (k == 1 ? a.y : a.z); }
inline V3 Cross( V3 a, V3 b ) { return Make(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline V3 Normalize( V3 a, float *length = nullptr )
{
	const float l = Length(a);
	if ( length )
		*length = l;
	return l > 1e-12f ? Scale(a, 1.0f / l) : Make(0.0f, 0.0f, 0.0f);
}
inline float Saturate( float v ) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float Luma( const float *c ) { return LUMA[0] * c[0] + LUMA[1] * c[1] + LUMA[2] * c[2]; }
inline float MaxComponent( V3 a ) { return std::max(a.x, std::max(a.y, a.z)); }

// 3x3 symmetric matrix, row major
struct M3
{
	float m[9];
};

inline void Zero( M3& a ) { memset(a.m, 0, sizeof(a.m)); }

// a += w (I - d d^T)
inline void AddProjector( M3& a, V3 d, float w )
{
	const float v[3] = { d.x, d.y, d.z };
	for ( int r = 0; r < 3; r++ )
		for ( int c = 0; c < 3; c++ )
			a.m[r * 3 + c] += w * ((r == c ? 1.0f : 0.0f) - v[r] * v[c]);
}

inline V3 Mul( const M3& a, V3 v )
{
	return Make(
		a.m[0] * v.x + a.m[1] * v.y + a.m[2] * v.z,
		a.m[3] * v.x + a.m[4] * v.y + a.m[5] * v.z,
		a.m[6] * v.x + a.m[7] * v.y + a.m[8] * v.z);
}

// (I - d d^T) v: the part of v perpendicular to the unit d
inline V3 Perpendicular( V3 d, V3 v )
{
	return Sub(v, Scale(d, Dot(d, v)));
}

bool Solve( const M3& a, V3 b, V3 *x )
{
	const float *m = a.m;
	const float c00 = m[4] * m[8] - m[5] * m[7];
	const float c01 = m[5] * m[6] - m[3] * m[8];
	const float c02 = m[3] * m[7] - m[4] * m[6];
	const double det = (double)m[0] * c00 + (double)m[1] * c01 + (double)m[2] * c02;
	if ( fabs(det) < 1e-20 )
		return false;
	const float inv = (float)(1.0 / det);
	M3 r;
	r.m[0] = c00 * inv;
	r.m[1] = (m[2] * m[7] - m[1] * m[8]) * inv;
	r.m[2] = (m[1] * m[5] - m[2] * m[4]) * inv;
	r.m[3] = c01 * inv;
	r.m[4] = (m[0] * m[8] - m[2] * m[6]) * inv;
	r.m[5] = (m[2] * m[3] - m[0] * m[5]) * inv;
	r.m[6] = c02 * inv;
	r.m[7] = (m[1] * m[6] - m[0] * m[7]) * inv;
	r.m[8] = (m[0] * m[4] - m[1] * m[3]) * inv;
	*x = Mul(r, b);
	return std::isfinite(x->x) && std::isfinite(x->y) && std::isfinite(x->z);
}

// eigenvalues of a symmetric 3x3 matrix (closed form), ascending
void Eigenvalues( const M3& a, float *minValue, float *maxValue )
{
	const double a00 = a.m[0], a11 = a.m[4], a22 = a.m[8];
	const double a01 = a.m[1], a02 = a.m[2], a12 = a.m[5];
	const double p1 = a01 * a01 + a02 * a02 + a12 * a12;
	if ( p1 < 1e-30 )
	{
		*minValue = (float)std::min(a00, std::min(a11, a22));
		*maxValue = (float)std::max(a00, std::max(a11, a22));
		return;
	}
	const double q = (a00 + a11 + a22) / 3.0;
	const double p2 = (a00 - q) * (a00 - q) + (a11 - q) * (a11 - q) + (a22 - q) * (a22 - q) + 2.0 * p1;
	const double p = sqrt(p2 / 6.0);
	const double b00 = (a00 - q) / p, b11 = (a11 - q) / p, b22 = (a22 - q) / p;
	const double b01 = a01 / p, b02 = a02 / p, b12 = a12 / p;
	double r = 0.5 * (b00 * (b11 * b22 - b12 * b12) - b01 * (b01 * b22 - b12 * b02) + b02 * (b01 * b12 - b11 * b02));
	r = std::max(-1.0, std::min(1.0, r));
	const double phi = acos(r) / 3.0;
	const double e1 = q + 2.0 * p * cos(phi);
	const double e3 = q + 2.0 * p * cos(phi + 2.0943951023931953);
	*minValue = (float)e3;
	*maxValue = (float)e1;
}

struct Ray
{
	V3 origin;
	V3 dir;
	float weight;
};

// a point source proxy or an area anchor with what the attribution needs
struct Source
{
	vrProxy proxy;
	V3 position;
	V3 chroma;			// unit length
	float energy;		// explained brightness (merge / cap order)
	// point: monotonic radial profile of the brightness projected on the chroma
	float profileR[PROFILE_BINS];
	float profileV[PROFILE_BINS];
	int profileCount;
	// area: predicted = calibration * E_area(x)
	float calibration;
	int startCell;
};

struct Ctx
{
	const vrInput *in;
	int bx, by, bz, n;
	V3 origin;
	V3 size;
	float diag;
	const float *B;				// 3 per cell
	std::vector<float> Q;		// 3 per cell
	std::vector<V3> grad[4];		// R, G, B, luminance
	std::vector<V3> unitGrad[4];	// normalized
	std::vector<float> strength[4];	// |grad| relative to the local brightness, 0..1
	float eps[4];					// brightness floor of the gradient strength
	float meanLuma;					// mean luminance of B over the valid cells
	std::vector<V3> positions;
	std::vector<int> stamp;
	int currentStamp;

	int Index( int x, int y, int z ) const { return x + bx * (y + by * z); }
	void Coord( int i, int *x, int *y, int *z ) const { *x = i % bx; *y = (i / bx) % by; *z = i / (bx * by); }
	bool Valid( int i ) const { return in->valid[i] != 0; }
	V3 Position( int i ) const { return positions[i]; }
	V3 BspDir( int i ) const { return Load(in->bspDir + i * 3); }
	float Channel( const float *field, int i, int ch ) const
	{
		return ch < 3 ? field[i * 3 + ch] : Luma(field + i * 3);
	}
	float QChannel( int i, int ch ) const { return Channel(Q.data(), i, ch); }
	float BChannel( int i, int ch ) const { return Channel(B, i, ch); }
	float Strength( int i, int ch ) const { return strength[ch][i]; }
	int NewStamp()
	{
		if ( ++currentStamp == 0x7fffffff )
		{
			std::fill(stamp.begin(), stamp.end(), 0);
			currentStamp = 1;
		}
		return currentStamp;
	}
};

// the valid cell nearest to a world position, within two cells of it; -1 if none
int NearestValid( const Ctx& ctx, V3 pos )
{
	const int cx = (int)floorf((pos.x - ctx.origin.x) / ctx.size.x + 0.5f);
	const int cy = (int)floorf((pos.y - ctx.origin.y) / ctx.size.y + 0.5f);
	const int cz = (int)floorf((pos.z - ctx.origin.z) / ctx.size.z + 0.5f);
	int best = -1;
	float bestDist = 1e30f;
	for ( int z = cz - 2; z <= cz + 2; z++ )
		for ( int y = cy - 2; y <= cy + 2; y++ )
			for ( int x = cx - 2; x <= cx + 2; x++ )
			{
				if ( x < 0 || y < 0 || z < 0 || x >= ctx.bx || y >= ctx.by || z >= ctx.bz )
					continue;
				const int i = ctx.Index(x, y, z);
				if ( !ctx.Valid(i) )
					continue;
				V3 d = Sub(ctx.Position(i), pos);
				const float dist = Dot(d, d);
				if ( dist < bestDist )
				{
					bestDist = dist;
					best = i;
				}
			}
	return best;
}

// valid cells reachable from start through valid six-neighbours for which inside() holds,
// nearest first (breadth first); walls are not crossed
template<typename Inside>
void Flood( Ctx& ctx, int start, Inside inside, int maxCells, std::vector<int>& out )
{
	out.clear();
	if ( start < 0 || !ctx.Valid(start) || !inside(start) )
		return;
	const int stamp = ctx.NewStamp();
	ctx.stamp[start] = stamp;
	out.push_back(start);
	for ( size_t head = 0; head < out.size() && (int)out.size() < maxCells; head++ )
	{
		int x, y, z;
		ctx.Coord(out[head], &x, &y, &z);
		const int nb[6][3] = { { x - 1, y, z }, { x + 1, y, z }, { x, y - 1, z }, { x, y + 1, z }, { x, y, z - 1 }, { x, y, z + 1 } };
		for ( int k = 0; k < 6 && (int)out.size() < maxCells; k++ )
		{
			if ( nb[k][0] < 0 || nb[k][1] < 0 || nb[k][2] < 0 ||
				nb[k][0] >= ctx.bx || nb[k][1] >= ctx.by || nb[k][2] >= ctx.bz )
				continue;
			const int j = ctx.Index(nb[k][0], nb[k][1], nb[k][2]);
			if ( ctx.stamp[j] == stamp || !ctx.Valid(j) )
				continue;
			ctx.stamp[j] = stamp;
			if ( inside(j) )
				out.push_back(j);
		}
	}
}

/*
-----------------------------------------------------------------------------
Stage D: gradients
-----------------------------------------------------------------------------
*/

// derivative of a per cell scalar along one axis: central, one-sided, or none
template<typename Field>
float AxisDerivative( const Ctx& ctx, int i, int axis, Field field, bool *known )
{
	int x, y, z;
	ctx.Coord(i, &x, &y, &z);
	int c[3] = { x, y, z };
	const int dim[3] = { ctx.bx, ctx.by, ctx.bz };
	const float h = Get(ctx.size, axis);
	int lo = -1, hi = -1;
	if ( c[axis] > 0 )
	{
		c[axis]--;
		const int j = ctx.Index(c[0], c[1], c[2]);
		if ( ctx.Valid(j) )
			lo = j;
		c[axis]++;
	}
	if ( c[axis] + 1 < dim[axis] )
	{
		c[axis]++;
		const int j = ctx.Index(c[0], c[1], c[2]);
		if ( ctx.Valid(j) )
			hi = j;
		c[axis]--;
	}
	*known = true;
	if ( lo >= 0 && hi >= 0 )
		return (field(hi) - field(lo)) / (2.0f * h);
	if ( hi >= 0 )
		return (field(hi) - field(i)) / h;
	if ( lo >= 0 )
		return (field(i) - field(lo)) / h;
	*known = false;
	return 0.0f;
}

void ComputeGradients( Ctx& ctx )
{
	for ( int ch = 0; ch < 4; ch++ )
		ctx.grad[ch].assign(ctx.n, Make(0.0f, 0.0f, 0.0f));
	for ( int i = 0; i < ctx.n; i++ )
	{
		if ( !ctx.Valid(i) )
			continue;
		for ( int ch = 0; ch < 4; ch++ )
		{
			auto field = [&]( int j ) { return ctx.BChannel(j, ch); };
			float g[3];
			for ( int axis = 0; axis < 3; axis++ )
			{
				bool known;
				g[axis] = AxisDerivative(ctx, i, axis, field, &known);
			}
			ctx.grad[ch][i] = Make(g[0], g[1], g[2]);
		}
	}
	for ( int ch = 0; ch < 4; ch++ )
	{
		ctx.unitGrad[ch].resize(ctx.n);
		ctx.strength[ch].resize(ctx.n);
		for ( int i = 0; i < ctx.n; i++ )
		{
			float length;
			ctx.unitGrad[ch][i] = Normalize(ctx.grad[ch][i], &length);
			ctx.strength[ch][i] = Saturate(length * ctx.diag / (ctx.BChannel(i, ch) + ctx.eps[ch]));
		}
	}
}

// -div of a unit vector field * half a cell diagonal: about diag / r at distance r of a
// point the field converges to (the divergence of the inward unit field is -2 / r)
template<typename Dir>
float Convergence( const Ctx& ctx, int i, Dir dir )
{
	float div = 0.0f;
	for ( int axis = 0; axis < 3; axis++ )
	{
		bool known;
		auto component = [&]( int j ) { return Get(dir(j), axis); };
		div += AxisDerivative(ctx, i, axis, component, &known);
	}
	return -div * 0.5f * ctx.diag;
}

/*
-----------------------------------------------------------------------------
Stage E: seeds
-----------------------------------------------------------------------------
*/

struct Seed
{
	int cell;
	int channel;	// 0..2 rgb, 3 luminance
	float score;
};

void FindSeeds( Ctx& ctx, std::vector<Seed>& seeds )
{
	seeds.clear();
	std::vector<float> score(ctx.n);
	for ( int ch = 0; ch < 4; ch++ )
	{
		float maxQ = 0.0f;
		for ( int i = 0; i < ctx.n; i++ )
			if ( ctx.Valid(i) )
				maxQ = std::max(maxQ, ctx.QChannel(i, ch));
		if ( maxQ <= 1e-6f )
			continue;

		for ( int i = 0; i < ctx.n; i++ )
		{
			score[i] = 0.0f;
			const float q = ctx.QChannel(i, ch);
			if ( !ctx.Valid(i) || q < SEED_MIN_RELATIVE * maxQ || q <= 1e-5f )
				continue;
			const float convBsp = Convergence(ctx, i, [&]( int j ) { return ctx.BspDir(j); });
			float convGrad = 0.0f;
			if ( ctx.Strength(i, ch) > GRADIENT_MIN_STRENGTH )
			{
				convGrad = Convergence(ctx, i, [&]( int j ) {
					return ctx.Strength(j, ch) > GRADIENT_MIN_STRENGTH ? ctx.unitGrad[ch][j] : Make(0.0f, 0.0f, 0.0f); });
			}
			score[i] = (q / maxQ) * (0.5f + 0.5f * Saturate(convBsp)) * (0.5f + 0.5f * Saturate(convGrad));
		}

		// non-maximum suppression over the 26 neighbours (about one cell)
		for ( int i = 0; i < ctx.n; i++ )
		{
			if ( score[i] <= 0.0f )
				continue;
			int x, y, z;
			ctx.Coord(i, &x, &y, &z);
			bool isMax = true;
			for ( int dz = -1; dz <= 1 && isMax; dz++ )
				for ( int dy = -1; dy <= 1 && isMax; dy++ )
					for ( int dx = -1; dx <= 1 && isMax; dx++ )
					{
						const int nx = x + dx, ny = y + dy, nz = z + dz;
						if ( (dx | dy | dz) == 0 || nx < 0 || ny < 0 || nz < 0 || nx >= ctx.bx || ny >= ctx.by || nz >= ctx.bz )
							continue;
						const int j = ctx.Index(nx, ny, nz);
						if ( score[j] > score[i] || (score[j] == score[i] && j < i) )
							isMax = false;
					}
			if ( isMax )
			{
				Seed s = { i, ch, score[i] };
				seeds.push_back(s);
			}
		}
	}

	std::stable_sort(seeds.begin(), seeds.end(), []( const Seed& a, const Seed& b ) { return a.score > b.score; });
	if ( (int)seeds.size() > SEED_MAX )
		seeds.resize(SEED_MAX);
}

/*
-----------------------------------------------------------------------------
Stage F: point proxy fit
-----------------------------------------------------------------------------
*/

// pool adjacent violators: weighted non-increasing fit of values
void IsotonicDecreasing( const float *values, const float *weights, int count, float *out )
{
	std::vector<float> v(values, values + count), w(weights, weights + count);
	std::vector<int> length(count, 1);
	int blocks = 0;
	for ( int i = 0; i < count; i++ )
	{
		v[blocks] = values[i];
		w[blocks] = weights[i];
		length[blocks] = 1;
		blocks++;
		while ( blocks > 1 && v[blocks - 2] < v[blocks - 1] )
		{
			const float wsum = w[blocks - 2] + w[blocks - 1];
			v[blocks - 2] = (v[blocks - 2] * w[blocks - 2] + v[blocks - 1] * w[blocks - 1]) / std::max(wsum, 1e-12f);
			w[blocks - 2] = wsum;
			length[blocks - 2] += length[blocks - 1];
			blocks--;
		}
	}
	int k = 0;
	for ( int b = 0; b < blocks; b++ )
		for ( int l = 0; l < length[b]; l++ )
			out[k++] = v[b];
}

// predicted brightness (projected on the chroma) of a point proxy at distance r:
// the monotonic profile, inverse square beyond its last bin
float ProfileAt( const Source& s, float r )
{
	if ( s.profileCount <= 0 )
		return 0.0f;
	if ( r <= s.profileR[0] )
		return s.profileV[0];
	for ( int k = 1; k < s.profileCount; k++ )
	{
		if ( r <= s.profileR[k] )
		{
			const float t = (r - s.profileR[k - 1]) / std::max(s.profileR[k] - s.profileR[k - 1], 1e-6f);
			return s.profileV[k - 1] + (s.profileV[k] - s.profileV[k - 1]) * t;
		}
	}
	const float last = s.profileR[s.profileCount - 1];
	return s.profileV[s.profileCount - 1] * (last * last) / std::max(r * r, 1e-6f);
}

bool FitSeed( Ctx& ctx, const Seed& seed, Source *out, float *rms )
{
	const int ch = seed.channel;
	int sx, sy, sz;
	ctx.Coord(seed.cell, &sx, &sy, &sz);

	std::vector<int> support;
	support.reserve(SUPPORT_MAX);
	Flood(ctx, seed.cell, [&]( int j ) {
		int x, y, z;
		ctx.Coord(j, &x, &y, &z);
		const float d2 = (float)((x - sx) * (x - sx) + (y - sy) * (y - sy) + (z - sz) * (z - sz));
		return d2 <= SUPPORT_RADIUS * SUPPORT_RADIUS;
	}, SUPPORT_MAX, support);
	if ( support.size() < 4 )
		return false;

	float maxQ = 0.0f;
	for ( int j : support )
		maxQ = std::max(maxQ, ctx.QChannel(j, ch));
	if ( maxQ <= 0.0f )
		return false;

	// BSP direction rays and gradient rays, separate observations
	std::vector<Ray> rays;
	rays.reserve(support.size() * 2);
	for ( int j : support )
	{
		const V3 x = ctx.Position(j);
		const float q = ctx.QChannel(j, ch);
		if ( q > 0.0f )
		{
			Ray r = { x, ctx.BspDir(j), q / maxQ };
			rays.push_back(r);
		}
		if ( ctx.Strength(j, ch) > GRADIENT_MIN_STRENGTH )
		{
			float length;
			const V3 d = Normalize(ctx.grad[ch][j], &length);
			Ray r = { x, d, length * ctx.diag / maxQ };
			rays.push_back(r);
		}
	}
	if ( rays.size() < 4 )
		return false;

	// min_p sum w |(I - d d^T)(p - x)|^2, with a small ridge towards the seed so that
	// parallel rays (a doorway far field) stay near the brightest cell along them
	const V3 seedPos = ctx.Position(seed.cell);
	float wsum = 0.0f;
	for ( const Ray& r : rays )
		wsum += r.weight;
	if ( wsum <= 1e-6f )
		return false;
	const float ridge = 1e-3f * wsum;
	const float delta = HUBER_DELTA * ctx.diag;

	std::vector<float> huber(rays.size(), 1.0f);
	V3 p = seedPos;
	M3 A;
	for ( int iteration = 0; iteration < 4; iteration++ )
	{
		Zero(A);
		V3 b = Scale(seedPos, ridge);
		for ( size_t k = 0; k < rays.size(); k++ )
		{
			const float w = rays[k].weight * huber[k];
			AddProjector(A, rays[k].dir, w);
			b = Add(b, Scale(Perpendicular(rays[k].dir, rays[k].origin), w));
		}
		M3 Ar = A;
		Ar.m[0] += ridge;
		Ar.m[4] += ridge;
		Ar.m[8] += ridge;
		if ( !Solve(Ar, b, &p) )
			return false;
		for ( size_t k = 0; k < rays.size(); k++ )
		{
			const float r = Length(Perpendicular(rays[k].dir, Sub(p, rays[k].origin)));
			huber[k] = r <= delta ? 1.0f : delta / r;
		}
	}
	if ( Length(Sub(p, seedPos)) > 6.0f * ctx.diag )
		return false;

	// residual and conditioning
	double rss = 0.0, rw = 0.0;
	for ( size_t k = 0; k < rays.size(); k++ )
	{
		const float w = rays[k].weight * huber[k];
		const float r = Length(Perpendicular(rays[k].dir, Sub(p, rays[k].origin)));
		rss += w * r * r;
		rw += w;
	}
	*rms = (float)sqrt(rss / std::max(rw, 1e-12));
	float lambdaMin, lambdaMax;
	Eigenvalues(A, &lambdaMin, &lambdaMax);
	const float conditioning = lambdaMax > 1e-12f ? Saturate((lambdaMin / lambdaMax) / CONDITION_THRESHOLD) : 0.0f;

	// consistency: the observations point towards p (lines constrain both ways, rays do not)
	float consistent = 0.0f, consistentWeight = 0.0f;
	for ( size_t k = 0; k < rays.size(); k++ )
	{
		const V3 u = Normalize(Sub(p, rays[k].origin));
		consistent += rays[k].weight * (Dot(rays[k].dir, u) > 0.25f ? 1.0f : 0.0f);
		consistentWeight += rays[k].weight;
	}
	const float cGradient = consistentWeight > 0.0f ? consistent / consistentWeight : 0.0f;

	// chromaticity from the positive radial derivatives towards p (near and far half apart)
	std::vector<float> distances;
	distances.reserve(support.size());
	for ( int j : support )
		distances.push_back(Length(Sub(p, ctx.Position(j))));
	std::vector<float> sorted = distances;
	std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
	const float medianDistance = sorted[sorted.size() / 2];
	V3 chromaNear = Make(0.0f, 0.0f, 0.0f), chromaFar = chromaNear, chromaDirect = chromaNear;
	for ( size_t s = 0; s < support.size(); s++ )
	{
		const int j = support[s];
		const V3 u = Normalize(Sub(p, ctx.Position(j)));
		const V3 q = Make(
			std::max(Dot(ctx.grad[0][j], u), 0.0f),
			std::max(Dot(ctx.grad[1][j], u), 0.0f),
			std::max(Dot(ctx.grad[2][j], u), 0.0f));
		if ( distances[s] <= medianDistance )
			chromaNear = Add(chromaNear, q);
		else
			chromaFar = Add(chromaFar, q);
		chromaDirect = Add(chromaDirect, Scale(Load(&ctx.Q[j * 3]), std::max(Dot(ctx.BspDir(j), u), 0.0f)));
	}
	V3 chroma = Add(chromaNear, chromaFar);
	float cChroma;
	if ( MaxComponent(chroma) > 1e-9f )
	{
		const V3 n = Normalize(chromaNear), f = Normalize(chromaFar);
		cChroma = (Length(n) > 0.0f && Length(f) > 0.0f) ? Saturate((Dot(n, f) - 0.8f) / 0.2f) : 0.5f;
	}
	else
	{
		// no usable gradients: the direct light the BSP directions attribute to p
		chroma = chromaDirect;
		cChroma = 0.5f;
	}
	if ( MaxComponent(chroma) <= 1e-9f )
		return false;
	const V3 chromaUnit = Normalize(chroma);

	// radial profile of the brightness projected on the chroma: robust median per bin,
	// non-increasing fit; its misfit and its decay are the profile confidence
	const float binWidth = 0.5f * ctx.diag;
	std::vector<float> bins[PROFILE_BINS];
	std::vector<float> values(support.size());
	for ( size_t s = 0; s < support.size(); s++ )
	{
		values[s] = std::max(Dot(Load(&ctx.Q[support[s] * 3]), chromaUnit), 0.0f);
		const int bin = std::min(PROFILE_BINS - 1, (int)(distances[s] / binWidth));
		bins[bin].push_back(values[s]);
	}
	float binR[PROFILE_BINS], binV[PROFILE_BINS], binW[PROFILE_BINS], iso[PROFILE_BINS];
	int binIndex[PROFILE_BINS];
	int count = 0;
	for ( int k = 0; k < PROFILE_BINS; k++ )
	{
		if ( bins[k].empty() )
			continue;
		std::vector<float>& b = bins[k];
		std::nth_element(b.begin(), b.begin() + b.size() / 2, b.end());
		binR[count] = (k + 0.5f) * binWidth;
		binV[count] = b[b.size() / 2];
		binW[count] = (float)b.size();
		binIndex[k] = count;
		count++;
	}
	if ( count < 2 )
		return false;
	IsotonicDecreasing(binV, binW, count, iso);
	double misfit = 0.0, total = 0.0;
	for ( size_t s = 0; s < support.size(); s++ )
	{
		const int bin = std::min(PROFILE_BINS - 1, (int)(distances[s] / binWidth));
		misfit += fabsf(values[s] - iso[binIndex[bin]]);
		total += values[s];
	}
	if ( total <= 1e-9 || iso[0] <= 1e-9f )
		return false;
	float cProfile = Saturate(1.0f - (float)(misfit / total));
	cProfile *= Saturate((1.0f - iso[count - 1] / iso[0]) / 0.3f);	// a flat field is no source

	const float cRay = expf(-(*rms / (SIGMA_RAY * ctx.diag)) * (*rms / (SIGMA_RAY * ctx.diag)));
	const float confidence = cRay * sqrtf(cGradient * cProfile * cChroma);
	if ( !(confidence >= MIN_SOURCE_CONFIDENCE) )
		return false;

	Source& s = *out;
	memset(&s, 0, sizeof(s));
	s.position = p;
	s.chroma = chromaUnit;
	s.energy = (float)total;
	s.calibration = 0.0f;
	s.profileCount = count;
	for ( int k = 0; k < count; k++ )
	{
		s.profileR[k] = binR[k];
		s.profileV[k] = iso[k];
	}
	// badly conditioned fits (near parallel rays) are badly localized, not badly directed
	const float sigmaP = (0.25f * ctx.diag + *rms) / std::max(sqrtf(conditioning), 0.15f);

	// range: where the predicted light falls below RANGE_FALLOFF of the mean baked light of the
	// map (a peak relative floor would be meaningless for an inverse square field)
	float range;
	const float target = std::max(RANGE_FALLOFF * ctx.meanLuma, 1e-9f);
	if ( iso[count - 1] <= target )
	{
		range = binR[count - 1];
		for ( int k = 0; k < count; k++ )
			if ( iso[k] <= target )
			{
				range = binR[k];
				break;
			}
	}
	else
		range = binR[count - 1] * sqrtf(iso[count - 1] / target);
	range = std::max(RANGE_MIN * ctx.diag, std::min(RANGE_MAX * ctx.diag, range));

	vrProxy& proxy = s.proxy;
	proxy.position[0] = p.x; proxy.position[1] = p.y; proxy.position[2] = p.z;
	const float cmax = MaxComponent(chromaUnit);
	proxy.color[0] = chromaUnit.x / cmax; proxy.color[1] = chromaUnit.y / cmax; proxy.color[2] = chromaUnit.z / cmax;
	proxy.confidence = confidence;
	proxy.sigmaP = sigmaP;
	proxy.rayRms = *rms;
	proxy.range = range;
	proxy.area = -1;
	proxy.support = (int)support.size();
	s.startCell = NearestValid(ctx, p);
	if ( s.startCell < 0 )
		s.startCell = seed.cell;
	return true;
}

/*
-----------------------------------------------------------------------------
Area anchors
-----------------------------------------------------------------------------
*/

// first angular moment of a rectangle emitter seen from x: E = sum w_k,
// M = sum w_k u_k with w_k = area_k cos_emitter / r^2 (3x3 quadrature near it)
void AreaMoment( const vrAreaSource& a, V3 x, float *energy, V3 *moment )
{
	const V3 center = Load(a.center), right = Load(a.right), up = Load(a.up);
	const V3 normal = Normalize(Cross(right, up));
	const float area = 4.0f * a.halfWidth * a.halfHeight;
	const float extent = std::max(a.halfWidth, a.halfHeight) * 2.0f;
	const float distance = Length(Sub(center, x));
	const int n = (distance > 1e-3f && extent / distance < 0.25f) ? 1 : 3;
	const float sampleArea = area / (float)(n * n);
	*energy = 0.0f;
	*moment = Make(0.0f, 0.0f, 0.0f);
	for ( int j = 0; j < n; j++ )
		for ( int i = 0; i < n; i++ )
		{
			const float s = n == 1 ? 0.0f : ((i + 0.5f) / n * 2.0f - 1.0f);
			const float t = n == 1 ? 0.0f : ((j + 0.5f) / n * 2.0f - 1.0f);
			const V3 sample = Add(center, Add(Scale(right, s * a.halfWidth), Scale(up, t * a.halfHeight)));
			float r;
			const V3 u = Normalize(Sub(sample, x), &r);
			if ( r < 1e-3f )
				continue;
			float cosine = Dot(normal, Scale(u, -1.0f));
			cosine = a.twoSided ? fabsf(cosine) : std::max(cosine, 0.0f);
			const float w = sampleArea * cosine / std::max(r * r, 1.0f);
			*energy += w;
			*moment = Add(*moment, Scale(u, w));
		}
}

bool BuildAreaAnchor( Ctx& ctx, int index, Source *out )
{
	const vrAreaSource& a = ctx.in->areas[index];
	if ( a.halfWidth <= 0.0f || a.halfHeight <= 0.0f || !(a.confidence > 0.0f) )
		return false;
	const V3 center = Load(a.center);
	const V3 normal = Normalize(Cross(Load(a.right), Load(a.up)));
	if ( Length(normal) <= 0.0f )
		return false;
	const int start = NearestValid(ctx, Add(center, Scale(normal, 0.5f * ctx.diag)));
	if ( start < 0 )
		return false;
	int sx, sy, sz;
	ctx.Coord(start, &sx, &sy, &sz);
	std::vector<int> support;
	Flood(ctx, start, [&]( int j ) {
		int x, y, z;
		ctx.Coord(j, &x, &y, &z);
		const float d2 = (float)((x - sx) * (x - sx) + (y - sy) * (y - sy) + (z - sz) * (z - sz));
		return d2 <= AREA_SUPPORT_RADIUS * AREA_SUPPORT_RADIUS;
	}, SUPPORT_MAX, support);
	if ( support.empty() )
		return false;

	// the chroma the grid shows around the emitter, weighted by its geometric reach
	V3 sumQ = Make(0.0f, 0.0f, 0.0f);
	for ( int j : support )
	{
		float e;
		V3 m;
		AreaMoment(a, ctx.Position(j), &e, &m);
		sumQ = Add(sumQ, Scale(Load(&ctx.Q[j * 3]), e));
	}
	if ( MaxComponent(sumQ) <= 1e-12f )
		return false;
	const V3 chroma = Normalize(sumQ);

	// calibration (median ratio of the projected brightness to the geometric reach) and the
	// agreement of the BSP directions / gradients with the emitter direction
	std::vector<float> ratios;
	float agree = 0.0f, agreeWeight = 0.0f;
	for ( int j : support )
	{
		float e;
		V3 m;
		AreaMoment(a, ctx.Position(j), &e, &m);
		if ( e <= 0.0f )
			continue;
		const float v = std::max(Dot(Load(&ctx.Q[j * 3]), chroma), 0.0f);
		if ( v <= 0.0f )
			continue;
		ratios.push_back(v / e);
		const V3 u = Normalize(m);
		const float s = ctx.Strength(j, 3);
		const float bsp = Saturate(2.0f * Dot(ctx.BspDir(j), u) - 1.0f);
		const float grad = Saturate(2.0f * Dot(ctx.unitGrad[3][j], u) - 1.0f);
		agree += v * (s * grad + (1.0f - s) * bsp);
		agreeWeight += v;
	}
	if ( ratios.empty() || agreeWeight <= 0.0f )
		return false;
	std::nth_element(ratios.begin(), ratios.begin() + ratios.size() / 2, ratios.end());
	const float calibration = ratios[ratios.size() / 2];
	if ( !(calibration > 0.0f) )
		return false;

	float colorAgree = 0.5f;
	const V3 emitterColor = Load(a.color);
	if ( MaxComponent(emitterColor) > 1e-6f )
		colorAgree = Saturate((Dot(Normalize(emitterColor), chroma) - 0.7f) / 0.3f);
	const float confidence = std::min(1.0f, a.confidence) * sqrtf((agree / agreeWeight) * (0.5f + 0.5f * colorAgree));
	if ( !(confidence >= MIN_SOURCE_CONFIDENCE) )
		return false;

	Source& s = *out;
	memset(&s, 0, sizeof(s));
	s.position = center;
	s.chroma = chroma;
	s.calibration = calibration;
	s.energy = agreeWeight;
	s.startCell = start;
	vrProxy& proxy = s.proxy;
	proxy.position[0] = center.x; proxy.position[1] = center.y; proxy.position[2] = center.z;
	const float cmax = MaxComponent(chroma);
	proxy.color[0] = chroma.x / cmax; proxy.color[1] = chroma.y / cmax; proxy.color[2] = chroma.z / cmax;
	proxy.confidence = confidence;
	proxy.sigmaP = 0.0f;
	proxy.rayRms = 0.0f;
	const float extent = std::max(a.halfWidth, a.halfHeight);
	// far field E ~ area / d^2: the distance where calibration * E reaches the floor
	const float area = 4.0f * a.halfWidth * a.halfHeight;
	const float target = std::max(RANGE_FALLOFF * ctx.meanLuma, 1e-9f);
	proxy.range = std::max(RANGE_MIN * ctx.diag, std::min(RANGE_MAX * ctx.diag,
		extent + sqrtf(calibration * area / target)));
	proxy.area = index;
	proxy.support = (int)support.size();
	return true;
}

/*
-----------------------------------------------------------------------------
Stages G / H: attribution and moments
-----------------------------------------------------------------------------
*/

// per cell and channel: max m, sum m, sum m * direction
struct Accumulator
{
	float maxM[3];
	float sumM[3];
	float sumMU[3][3];
};

void Attribute( Ctx& ctx, const Source& src, std::vector<Accumulator>& acc )
{
	const vrProxy& proxy = src.proxy;
	const bool area = proxy.area >= 0;
	const float range2 = proxy.range * proxy.range;
	std::vector<int> domain;
	Flood(ctx, src.startCell, [&]( int j ) {
		const V3 d = Sub(ctx.Position(j), src.position);
		return Dot(d, d) <= range2;
	}, DOMAIN_MAX, domain);

	const float chromaSum = src.chroma.x + src.chroma.y + src.chroma.z;
	for ( int i : domain )
	{
		const float *q = &ctx.Q[i * 3];
		const float qSum = q[0] + q[1] + q[2];
		if ( qSum <= 0.0f )
			continue;
		const V3 x = ctx.Position(i);

		V3 u, dirVec;
		float cPos, predicted;
		if ( area )
		{
			float e;
			V3 m;
			AreaMoment(ctx.in->areas[proxy.area], x, &e, &m);
			if ( e <= 0.0f )
				continue;
			// the quadrature moment is shorter where the emitter subtends a large angle
			dirVec = Scale(m, 1.0f / e);
			u = Normalize(dirVec);
			cPos = 1.0f;
			predicted = src.calibration * e;
		}
		else
		{
			float r;
			u = Normalize(Sub(src.position, x), &r);
			if ( r < 1e-3f )
				continue;
			const float s2 = proxy.sigmaP * proxy.sigmaP;
			cPos = r * r / (r * r + s2);
			dirVec = Scale(u, r / sqrtf(r * r + s2));
			predicted = ProfileAt(src, r);
		}

		// plausibility: can this source explain the brightness seen here
		const float observed = std::max(Dot(Load(q), src.chroma), 1e-9f);
		const float falloff = Saturate(predicted / observed);
		const float bsp = Saturate(2.0f * Dot(ctx.BspDir(i), u) - 1.0f);
		const float base = proxy.confidence * cPos * falloff;
		if ( base <= 0.0f )
			continue;

		Accumulator& a = acc[i];
		for ( int c = 0; c < 3; c++ )
		{
			if ( q[c] <= 0.0f )
				continue;
			// RGB gradient where informative, the shared BSP direction where not
			const float s = ctx.Strength(i, c);
			const float grad = s > 0.0f ? Saturate(2.0f * Dot(ctx.unitGrad[c][i], u) - 1.0f) : 0.0f;
			const float evidence = s * grad + (1.0f - s) * bsp;
			const float compat = Saturate((Get(src.chroma, c) / chromaSum) / std::max(q[c] / qSum, 1e-6f));
			const float m = Saturate(base * evidence * compat);
			if ( m <= 0.0f )
				continue;
			a.maxM[c] = std::max(a.maxM[c], m);
			a.sumM[c] += m;
			a.sumMU[c][0] += m * dirVec.x;
			a.sumMU[c][1] += m * dirVec.y;
			a.sumMU[c][2] += m * dirVec.z;
		}
	}
}

void Reconstruct( Ctx& ctx, vrOutput& out )
{
	vrStats& st = out.stats;
	Clock::time_point t = Clock::now();
	ComputeGradients(ctx);
	st.msecGradients = Msec(t);

	t = Clock::now();
	std::vector<Seed> seeds;
	FindSeeds(ctx, seeds);
	st.seeds = (int)seeds.size();
	st.msecSeeds = Msec(t);

	t = Clock::now();
	std::vector<Source> fitted;
	for ( const Seed& seed : seeds )
	{
		Source s;
		float rms;
		if ( FitSeed(ctx, seed, &s, &rms) )
			fitted.push_back(s);
	}
	st.fits = (int)fitted.size();

	// merge fits of one light: close and of the same colour (two white lamps apart stay two)
	std::stable_sort(fitted.begin(), fitted.end(), []( const Source& a, const Source& b ) {
		return a.proxy.confidence * a.energy > b.proxy.confidence * b.energy; });
	std::vector<Source> points;
	std::vector<float> mergeWeight;
	for ( const Source& s : fitted )
	{
		const float w = s.proxy.confidence * s.energy;
		bool merged = false;
		for ( size_t k = 0; k < points.size(); k++ )
		{
			Source& o = points[k];
			// overlapping uncertainty: one light fitted twice from different seeds
			const float distance = std::min(MERGE_DISTANCE_MAX * ctx.diag,
				std::max(MERGE_DISTANCE * ctx.diag, o.proxy.sigmaP + s.proxy.sigmaP));
			if ( Length(Sub(o.position, s.position)) < distance && Dot(o.chroma, s.chroma) > MERGE_CHROMA_COS )
			{
				const float total = mergeWeight[k] + w;
				if ( total > 0.0f )
					o.position = Add(Scale(o.position, mergeWeight[k] / total), Scale(s.position, w / total));
				o.proxy.position[0] = o.position.x; o.proxy.position[1] = o.position.y; o.proxy.position[2] = o.position.z;
				o.proxy.range = std::max(o.proxy.range, s.proxy.range);
				o.energy += s.energy;
				mergeWeight[k] = total;
				merged = true;
				break;
			}
		}
		if ( !merged )
		{
			points.push_back(s);
			mergeWeight.push_back(w);
		}
	}
	st.msecFit = Msec(t);

	// area anchors, preferred over a point proxy of the same light
	t = Clock::now();
	std::vector<Source> anchors;
	for ( int a = 0; a < ctx.in->numAreas; a++ )
	{
		Source s;
		if ( BuildAreaAnchor(ctx, a, &s) )
			anchors.push_back(s);
	}
	std::stable_sort(anchors.begin(), anchors.end(), []( const Source& a, const Source& b ) {
		return a.proxy.confidence * a.energy > b.proxy.confidence * b.energy; });
	if ( (int)anchors.size() > PROXY_MAX )
		anchors.resize(PROXY_MAX);
	std::vector<Source> sources;
	for ( const Source& p : points )
	{
		bool duplicate = false;
		for ( const Source& a : anchors )
		{
			const vrAreaSource& area = ctx.in->areas[a.proxy.area];
			const float reach = std::max(area.halfWidth, area.halfHeight) + MERGE_DISTANCE * ctx.diag;
			if ( Length(Sub(p.position, a.position)) < reach && Dot(p.chroma, a.chroma) > 0.9f )
			{
				duplicate = true;
				break;
			}
		}
		if ( !duplicate && (int)sources.size() < PROXY_MAX )
			sources.push_back(p);
	}
	st.pointProxies = (int)sources.size();
	st.areaAnchors = (int)anchors.size();
	sources.insert(sources.end(), anchors.begin(), anchors.end());
	st.msecAreas = Msec(t);

	// fit statistics of the accepted point proxies
	std::vector<float> rmsList;
	double sigmaSum = 0.0;
	for ( const Source& s : sources )
	{
		if ( s.proxy.area >= 0 )
			continue;
		rmsList.push_back(s.proxy.rayRms / ctx.diag);
		sigmaSum += s.proxy.sigmaP / ctx.diag;
	}
	if ( !rmsList.empty() )
	{
		double sum = 0.0;
		for ( float r : rmsList )
			sum += r;
		st.meanRayRms = (float)(sum / rmsList.size());
		std::sort(rmsList.begin(), rmsList.end());
		st.p95RayRms = rmsList[std::min(rmsList.size() - 1, (rmsList.size() * 95) / 100)];
		st.meanSigmaP = (float)(sigmaSum / rmsList.size());
	}

	t = Clock::now();
	std::vector<Accumulator> acc(ctx.n);
	memset(acc.data(), 0, acc.size() * sizeof(Accumulator));
	for ( const Source& s : sources )
		Attribute(ctx, s, acc);

	// E = Q * max m, split by m: M = E * sum(m u) / sum(m)
	double energySum = 0.0;
	for ( int i = 0; i < ctx.n; i++ )
	{
		const Accumulator& a = acc[i];
		float e[3] = { 0.0f, 0.0f, 0.0f };
		for ( int c = 0; c < 3; c++ )
		{
			float *m = &out.moment[c][i * 3];
			m[0] = m[1] = m[2] = 0.0f;
			if ( a.sumM[c] <= 0.0f )
				continue;
			e[c] = ctx.Q[i * 3 + c] * a.maxM[c];
			const float k = e[c] / a.sumM[c];
			for ( int d = 0; d < 3; d++ )
				m[d] = a.sumMU[c][d] * k;
		}
		energySum += Luma(e);
	}
	st.attributedFraction = (float)energySum;
	st.msecAttribution = Msec(t);

	for ( const Source& s : sources )
		out.proxies.push_back(s.proxy);
}

} // namespace

void VR_Reconstruct( const vrInput& in, vrOutput& out )
{
	const Clock::time_point start = Clock::now();
	Ctx ctx;
	ctx.in = &in;
	ctx.bx = std::max(1, in.dims[0]);
	ctx.by = std::max(1, in.dims[1]);
	ctx.bz = std::max(1, in.dims[2]);
	ctx.n = ctx.bx * ctx.by * ctx.bz;
	ctx.origin = Load(in.origin);
	ctx.size = Load(in.cellSize);
	ctx.diag = Length(ctx.size);
	ctx.currentStamp = 0;

	const int n = ctx.n;
	out.legacy.assign(n * 3, 0.0f);
	out.baseline.assign(n * 3, 0.0f);
	out.sun.assign(n * 3, 0.0f);
	out.sunFraction.assign(n, 0.0f);
	for ( int c = 0; c < 3; c++ )
		out.moment[c].clear();
	out.proxies.clear();
	memset(&out.stats, 0, sizeof(out.stats));
	vrStats& st = out.stats;
	st.cells = n;

	// split: S = clamp(f D, 0, legacy), B = legacy - S, budget Q <= B
	Clock::time_point t = Clock::now();
	ctx.Q.assign(n * 3, 0.0f);
	for ( int i = 0; i < n; i++ )
	{
		const float *A = in.ambient + i * 3;
		const float *D = in.direct + i * 3;
		const float align = in.sunAlign ? Saturate(in.sunAlign[i]) : 0.0f;
		const float vis = in.sunVis ? Saturate(in.sunVis[i]) : 1.0f;
		const float f = align * vis;
		out.sunFraction[i] = f;
		const bool valid = in.valid[i] != 0;
		if ( valid )
			st.validCells++;
		for ( int c = 0; c < 3; c++ )
		{
			const float a = std::max(A[c], 0.0f), d = std::max(D[c], 0.0f);
			const float legacy = in.hdr ? a + d : std::max(a, d);
			const float s = std::min(std::max(f * d, 0.0f), legacy);
			const float b = legacy - s;
			out.legacy[i * 3 + c] = legacy;
			out.sun[i * 3 + c] = s;
			out.baseline[i * 3 + c] = b;
			st.maxSplitError = std::max(st.maxSplitError, fabsf(b + s - legacy));
			if ( valid )
			{
				const float budget = in.hdr ? (1.0f - f) * d : (1.0f - f) * std::max(d - a, 0.0f);
				ctx.Q[i * 3 + c] = std::min(std::max(budget, 0.0f), b);
			}
		}
	}
	ctx.B = out.baseline.data();
	st.msecSplit = Msec(t);

	double baselineSum = 0.0, budgetSum = 0.0;
	double channelSum[4] = { 0.0, 0.0, 0.0, 0.0 };
	for ( int i = 0; i < n; i++ )
	{
		if ( !ctx.Valid(i) )
			continue;
		baselineSum += Luma(ctx.B + i * 3);
		budgetSum += Luma(&ctx.Q[i * 3]);
		for ( int ch = 0; ch < 4; ch++ )
			channelSum[ch] += ctx.BChannel(i, ch);
	}
	for ( int ch = 0; ch < 4; ch++ )
		ctx.eps[ch] = 0.02f * (float)(channelSum[ch] / std::max(1, st.validCells)) + 1e-6f;
	ctx.meanLuma = (float)(channelSum[3] / std::max(1, st.validCells));

	if ( in.mode == 1 || in.mode == 2 )
	{
		for ( int c = 0; c < 3; c++ )
			out.moment[c].assign(n * 3, 0.0f);

		if ( in.mode == 2 )
		{
			// raw BSP direction: the whole budget along the one baked direction; filtering
			// between cells lit from different directions shortens it
			for ( int i = 0; i < n; i++ )
			{
				if ( !ctx.Valid(i) )
					continue;
				const V3 d = ctx.BspDir(i);
				for ( int c = 0; c < 3; c++ )
				{
					float *m = &out.moment[c][i * 3];
					const float q = ctx.Q[i * 3 + c];
					m[0] = d.x * q; m[1] = d.y * q; m[2] = d.z * q;
				}
			}
			st.attributedFraction = (float)budgetSum;
		}
		else
		{
			ctx.stamp.assign(n, 0);
			ctx.positions.resize(n);
			for ( int i = 0; i < n; i++ )
			{
				int x, y, z;
				ctx.Coord(i, &x, &y, &z);
				ctx.positions[i] = Make(ctx.origin.x + x * ctx.size.x, ctx.origin.y + y * ctx.size.y, ctx.origin.z + z * ctx.size.z);
			}
			Reconstruct(ctx, out);
		}

		// |M_c| <= B_c, whatever the rounding of the fit
		double momentSum = 0.0;
		for ( int i = 0; i < n; i++ )
		{
			float lengths[3];
			for ( int c = 0; c < 3; c++ )
			{
				float *m = &out.moment[c][i * 3];
				if ( !std::isfinite(m[0]) || !std::isfinite(m[1]) || !std::isfinite(m[2]) )
					m[0] = m[1] = m[2] = 0.0f;
				const float length = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
				const float b = out.baseline[i * 3 + c];
				if ( length > b )
				{
					const float k = length > 0.0f ? b / length : 0.0f;
					m[0] *= k; m[1] *= k; m[2] *= k;
				}
				lengths[c] = std::min(length, b);
			}
			const float lum = Luma(lengths);
			momentSum += lum;
			if ( lum > 0.01f * Luma(ctx.B + i * 3) && lum > 0.0f )
				st.directionalCells++;
		}
		if ( baselineSum > 0.0 )
		{
			st.directionalFraction = (float)(momentSum / baselineSum);
			st.attributedFraction = (float)(st.attributedFraction / baselineSum);
		}
		else
			st.attributedFraction = 0.0f;
	}

	st.msecTotal = Msec(start);
}

uint16_t VR_FloatToHalf( float f )
{
	uint32_t x;
	memcpy(&x, &f, sizeof(x));
	const uint16_t sign = (uint16_t)((x >> 16) & 0x8000);
	x &= 0x7fffffff;
	if ( x > 0x7f800000 )
		return (uint16_t)(sign | 0x7e00);		// NaN
	if ( x >= 0x477ff000 )
		return (uint16_t)(sign | 0x7bff);		// would round to infinity: 65504
	if ( x < 0x38800000 )
	{
		// half denormal (below 2^-14) or zero
		if ( x < 0x33000000 )
			return sign;
		const uint32_t e = x >> 23;
		const uint32_t m = (x & 0x7fffff) | 0x800000;
		const uint32_t shift = 126 - e;
		uint32_t h = m >> shift;
		const uint32_t rem = m & ((1u << shift) - 1);
		const uint32_t halfway = 1u << (shift - 1);
		if ( rem > halfway || (rem == halfway && (h & 1)) )
			h++;
		return (uint16_t)(sign | h);
	}
	uint32_t h = (x - 0x38000000) >> 13;
	const uint32_t rem = x & 0x1fff;
	if ( rem > 0x1000 || (rem == 0x1000 && (h & 1)) )
		h++;
	return (uint16_t)(sign | h);
}

float VR_HalfToFloat( uint16_t h )
{
	const int e = (h >> 10) & 0x1f;
	const uint32_t m = h & 0x3ff;
	float v;
	if ( e == 0 )
		v = ldexpf((float)m, -24);
	else if ( e == 31 )
		v = m ? NAN : INFINITY;
	else
		v = ldexpf((float)(m | 0x400), e - 25);
	return (h & 0x8000) ? -v : v;
}

void VR_PackHalf( const vrOutput& out, const float *alpha,
	std::vector<uint16_t>& baseline, std::vector<uint16_t> moments[3] )
{
	const size_t n = out.baseline.size() / 3;
	baseline.assign(n * 4, 0);
	const bool hasMoments = out.moment[0].size() == n * 3;
	for ( int c = 0; c < 3; c++ )
		moments[c].assign(hasMoments ? n * 4 : 0, 0);

	for ( size_t i = 0; i < n; i++ )
	{
		for ( int c = 0; c < 3; c++ )
			baseline[i * 4 + c] = VR_FloatToHalf(out.baseline[i * 3 + c]);
		baseline[i * 4 + 3] = VR_FloatToHalf(alpha ? alpha[i] : 1.0f);
		if ( !hasMoments )
			continue;

		for ( int c = 0; c < 3; c++ )
		{
			const float b = VR_HalfToFloat(baseline[i * 4 + c]);
			float m[3] = { out.moment[c][i * 3 + 0], out.moment[c][i * 3 + 1], out.moment[c][i * 3 + 2] };
			uint16_t *dst = &moments[c][i * 4];
			for ( int attempt = 0; ; attempt++ )
			{
				float len2 = 0.0f;
				for ( int k = 0; k < 3; k++ )
				{
					dst[k] = VR_FloatToHalf(m[k]);
					const float v = VR_HalfToFloat(dst[k]);
					len2 += v * v;
				}
				const float len = sqrtf(len2);
				if ( len <= b )
					break;
				if ( attempt >= 4 || len <= 0.0f )
				{
					dst[0] = dst[1] = dst[2] = 0;
					break;
				}
				const float k = (b / len) * (1.0f - 1e-3f * (attempt + 1));
				m[0] *= k; m[1] *= k; m[2] *= k;
			}
			dst[3] = 0;
		}
	}
}
