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
  gradients    six-neighbour finite differences of B.rgb (the luminance gradient is
               their linear combination), never through wall cells
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
  sources      one global budget of PROXY_MAX: area anchors first, point proxies fill
               the rest; a point fitted twice keeps its best fit whole
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
const int		PROXY_MAX = 256;			// point proxies + area anchors
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
const float		PROFILE_LIT = 0.02f;		// probes in the profile: at least this of the brightest

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
	int startCell2;		// two-sided area: the start on the back side, -1 if none / the same cell
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
	std::vector<V3> grad[3];		// R, G, B (luminance: their LUMA combination, GradChannel)
	std::vector<float> strength[3];	// |grad| relative to the local brightness, 0..1
	float eps[4];					// brightness floor of the gradient strength (R, G, B, luminance)
	float meanLuma;					// mean luminance of B over the valid cells
	std::vector<V3> positions;
	std::vector<int> stamp;
	int currentStamp;
	// scratch, reused between seeds / sources
	std::vector<int> support;
	std::vector<int> domain;
	std::vector<Ray> rays;
	std::vector<float> ratios;
	std::vector<float> scratchL;	// structured lights: the attributed part of one source, 3 per cell
	int traces;

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
	V3 GradChannel( int i, int ch ) const
	{
		if ( ch < 3 )
			return grad[ch][i];
		return Add(Add(Scale(grad[0][i], LUMA[0]), Scale(grad[1][i], LUMA[1])), Scale(grad[2][i], LUMA[2]));
	}
	float Strength( int i, int ch ) const
	{
		if ( ch < 3 )
			return strength[ch][i];
		return Saturate(Length(GradChannel(i, 3)) * diag / (BChannel(i, 3) + eps[3]));
	}
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
void FloodMulti( Ctx& ctx, const int *starts, int numStarts, Inside inside, int maxCells, std::vector<int>& out )
{
	out.clear();
	const int stamp = ctx.NewStamp();
	for ( int k = 0; k < numStarts; k++ )
	{
		const int start = starts[k];
		if ( start < 0 || !ctx.Valid(start) || ctx.stamp[start] == stamp || !inside(start) )
			continue;
		ctx.stamp[start] = stamp;
		out.push_back(start);
	}
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

template<typename Inside>
void Flood( Ctx& ctx, int start, Inside inside, int maxCells, std::vector<int>& out )
{
	FloodMulti(ctx, &start, 1, inside, maxCells, out);
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
	for ( int ch = 0; ch < 3; ch++ )
		ctx.grad[ch].assign(ctx.n, Make(0.0f, 0.0f, 0.0f));
	for ( int i = 0; i < ctx.n; i++ )
	{
		if ( !ctx.Valid(i) )
			continue;
		for ( int ch = 0; ch < 3; ch++ )
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
	for ( int ch = 0; ch < 3; ch++ )
	{
		ctx.strength[ch].resize(ctx.n);
		for ( int i = 0; i < ctx.n; i++ )
			ctx.strength[ch][i] = Saturate(Length(ctx.grad[ch][i]) * ctx.diag / (ctx.BChannel(i, ch) + ctx.eps[ch]));
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
	// the BSP direction field is the same for every channel: its convergence once per cell
	std::vector<float> convBsp(ctx.n, 0.0f);
	for ( int i = 0; i < ctx.n; i++ )
		if ( ctx.Valid(i) )
			convBsp[i] = Saturate(Convergence(ctx, i, [&]( int j ) { return ctx.BspDir(j); }));
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
			float convGrad = 0.0f;
			if ( ctx.Strength(i, ch) > GRADIENT_MIN_STRENGTH )
			{
				convGrad = Convergence(ctx, i, [&]( int j ) {
					return ctx.Strength(j, ch) > GRADIENT_MIN_STRENGTH ? Normalize(ctx.GradChannel(j, ch)) : Make(0.0f, 0.0f, 0.0f); });
			}
			score[i] = (q / maxQ) * (0.5f + 0.5f * convBsp[i]) * (0.5f + 0.5f * Saturate(convGrad));
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

	std::vector<int>& support = ctx.support;
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
	std::vector<Ray>& rays = ctx.rays;
	rays.clear();
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
			const V3 d = Normalize(ctx.GradChannel(j, ch), &length);
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
	// the conditioning with the final Huber weights (the loop built A with the previous ones)
	Zero(A);
	for ( size_t k = 0; k < rays.size(); k++ )
		AddProjector(A, rays[k].dir, rays[k].weight * huber[k]);

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
	// non-increasing fit; its misfit and its decay are the profile confidence. Only the lit
	// probes: beside a spot light the dark probes outside its cone are no falloff.
	const float binWidth = 0.5f * ctx.diag;
	std::vector<float> bins[PROFILE_BINS];
	std::vector<float> values(support.size());
	float maxValue = 0.0f;
	for ( size_t s = 0; s < support.size(); s++ )
	{
		values[s] = std::max(Dot(Load(&ctx.Q[support[s] * 3]), chromaUnit), 0.0f);
		maxValue = std::max(maxValue, values[s]);
	}
	const float litValue = PROFILE_LIT * maxValue;
	for ( size_t s = 0; s < support.size(); s++ )
	{
		if ( values[s] < litValue )
			continue;
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
		if ( values[s] < litValue )
			continue;
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
	proxy.type = VR_SOURCE_POINT;
	proxy.area = -1;
	proxy.support = (int)support.size();
	s.startCell2 = -1;
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
	// a two-sided emitter on a wall lights two grid components: a start on each side
	int starts[2] = { NearestValid(ctx, Add(center, Scale(normal, 0.5f * ctx.diag))), -1 };
	if ( a.twoSided )
	{
		starts[1] = NearestValid(ctx, Sub(center, Scale(normal, 0.5f * ctx.diag)));
		if ( starts[0] < 0 )
		{
			starts[0] = starts[1];
			starts[1] = -1;
		}
		if ( starts[1] == starts[0] )
			starts[1] = -1;
	}
	if ( starts[0] < 0 )
		return false;
	const int numStarts = starts[1] >= 0 ? 2 : 1;
	int sc[2][3];
	for ( int k = 0; k < numStarts; k++ )
		ctx.Coord(starts[k], &sc[k][0], &sc[k][1], &sc[k][2]);
	std::vector<int>& support = ctx.support;
	FloodMulti(ctx, starts, numStarts, [&]( int j ) {
		int x, y, z;
		ctx.Coord(j, &x, &y, &z);
		for ( int k = 0; k < numStarts; k++ )
		{
			const float d2 = (float)((x - sc[k][0]) * (x - sc[k][0]) + (y - sc[k][1]) * (y - sc[k][1]) +
				(z - sc[k][2]) * (z - sc[k][2]));
			if ( d2 <= AREA_SUPPORT_RADIUS * AREA_SUPPORT_RADIUS )
				return true;
		}
		return false;
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
	std::vector<float>& ratios = ctx.ratios;
	ratios.clear();
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
		const float grad = Saturate(2.0f * Dot(Normalize(ctx.GradChannel(j, 3)), u) - 1.0f);
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
	s.startCell = starts[0];
	s.startCell2 = starts[1];
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
	proxy.type = VR_SOURCE_RECT;
	proxy.area = index;
	proxy.support = (int)support.size();
	for ( int k = 0; k < 3; k++ )
	{
		proxy.right[k] = a.right[k];
		proxy.up[k] = a.up[k];
	}
	proxy.halfWidth = a.halfWidth;
	proxy.halfHeight = a.halfHeight;
	proxy.twoSided = a.twoSided;
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

// per cell of the source's domain (left in ctx.domain) and channel: the match score m in [0, 1]
// and the direction vector; f( cell, m[3], dirVec ) for the cells with any m > 0
template<typename F>
void MatchSource( Ctx& ctx, const Source& src, F f )
{
	const vrProxy& proxy = src.proxy;
	const bool area = proxy.area >= 0;
	const float range2 = proxy.range * proxy.range;
	std::vector<int>& domain = ctx.domain;
	const int starts[2] = { src.startCell, src.startCell2 };
	FloodMulti(ctx, starts, src.startCell2 >= 0 ? 2 : 1, [&]( int j ) {
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

		float ms[3] = { 0.0f, 0.0f, 0.0f };
		bool any = false;
		for ( int c = 0; c < 3; c++ )
		{
			if ( q[c] <= 0.0f )
				continue;
			// RGB gradient where informative, the shared BSP direction where not
			const float s = ctx.Strength(i, c);
			const float grad = s > 0.0f ? Saturate(2.0f * Dot(Normalize(ctx.grad[c][i]), u) - 1.0f) : 0.0f;
			const float evidence = s * grad + (1.0f - s) * bsp;
			const float compat = Saturate((Get(src.chroma, c) / chromaSum) / std::max(q[c] / qSum, 1e-6f));
			const float m = Saturate(base * evidence * compat);
			if ( m <= 0.0f )
				continue;
			ms[c] = m;
			any = true;
		}
		if ( any )
			f(i, ms, dirVec);
	}
}

void Attribute( Ctx& ctx, const Source& src, std::vector<Accumulator>& acc )
{
	MatchSource(ctx, src, [&]( int i, const float *m, V3 dirVec ) {
		Accumulator& a = acc[i];
		for ( int c = 0; c < 3; c++ )
		{
			if ( m[c] <= 0.0f )
				continue;
			a.maxM[c] = std::max(a.maxM[c], m[c]);
			a.sumM[c] += m[c];
			a.sumMU[c][0] += m[c] * dirVec.x;
			a.sumMU[c][1] += m[c] * dirVec.y;
			a.sumMU[c][2] += m[c] * dirVec.z;
		}
	});
}

// E = Q * max m, split by m: M = E * sum(m u) / sum(m); returns sum lum E
double ResolveMoments( Ctx& ctx, const std::vector<Accumulator>& acc, vrOutput& out )
{
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
	return energySum;
}



/*
-----------------------------------------------------------------------------
Structured lights
-----------------------------------------------------------------------------

Per point proxy: a stratified set of probes of its domain (bright and dark), their
attributed part L_ij of this source, the direct visibility of each from the source.
The source moves within its positional uncertainty when it does not see its probes,
then the runtime model color * A(r; R) [* spot cone] is fitted to the visible probes
only (a wall shadow is no falloff, an occluded sector no cone). The confident
physical lights are promoted: their modelled light at the probes they see leaves
the budget Q and the baseline B.
*/

const int		LIGHT_DISTANCE_BINS = 6;
const int		LIGHT_SAMPLES_PER_BIN = 4;	// per distance bin and octant
const int		LIGHT_RADIUS_STEPS = 28;
const float		LIGHT_RADIUS_MIN = 0.5f;	// of the attribution range
const float		LIGHT_RADIUS_MAX = 1.25f;	// beyond the attribution range there is nothing to match
const float		PHYSICAL_THRESHOLD = 0.6f;
const float		TRANSPORT_THRESHOLD = 0.35f;	// below: a transport proxy, no lamp
const float		SPOT_THRESHOLD = 0.6f;		// of the physical threshold
const int		PROMOTED_MAX = 32;
const float		RELOCATE_VISIBILITY = 0.8f;	// below: search a better origin within sigmaP
const float		RELOCATE_MAX = 0.75f;		// cell diagonals
const float		LEAK_MAX = 0.15f;			// modelled light at blocked probes (the runtime light is unshadowed)
const float		EXCESS_MAX = 0.2f;			// modelled light above the budget, of the light it takes out
const float		SPOT_CONCENTRATION = 0.35f;	// rho of the outgoing directions
const float		SPOT_IMPROVEMENT = 0.25f;	// E_spot <= (1 - this) * E_point
const float		SPOT_OUTER_MIN = 0.14f;		// radians, ~8 degrees
const float		SPOT_AXIS_COS = 0.94f;		// near and far axes within ~20 degrees
const float		MODEL_MIN = 0.02f;			// probes the runtime model reaches: A above this
const int		FIT_MIN_PROBES = 6;
const float		FIT_ENVELOPE = 0.25f;		// percentile of L / A: the brightness of the runtime light

struct LightSample
{
	int cell;
	V3 pos;
	float r;
	V3 dirOut;		// from the light towards the probe
	float L[3];		// attributed part of this source
	float lum;
	float vis;
};

struct RuntimeFit
{
	float radius;
	float color[3];
	float error;	// 1 - quality
	float quality;	// explained part of the attributed light minus twice the overshoot
};

inline float Smoothstep( float e0, float e1, float x )
{
	if ( e1 <= e0 )
		return x >= e1 ? 1.0f : 0.0f;
	const float t = Saturate((x - e0) / (e1 - e0));
	return t * t * (3.0f - 2.0f * t);
}

// CalcLightAttenuation of lightall / volumetric_inject: zero at the radius
inline float PointAttenuation( float r, float R )
{
	return Saturate(0.5f * R * R / std::max(r * r, 1e-6f) - 0.5f);
}

float Trace( Ctx& ctx, V3 a, V3 b )
{
	ctx.traces++;
	const float s[3] = { a.x, a.y, a.z }, e[3] = { b.x, b.y, b.z };
	return ctx.in->trace(ctx.in->traceUser, s, e);
}

void SetOrigin( std::vector<LightSample>& samples, V3 origin )
{
	for ( LightSample& s : samples )
		s.dirOut = Normalize(Sub(s.pos, origin), &s.r);
}

// energy weighted visibility of the lit probes from an origin; store: the visibility of every
// probe (dark ones too) goes into samples
float SampleVisibility( Ctx& ctx, std::vector<LightSample>& samples, V3 origin, bool store )
{
	double seen = 0.0, total = 0.0;
	for ( LightSample& s : samples )
	{
		if ( s.lum <= 0.0f && !store )
			continue;
		const float vis = Trace(ctx, origin, s.pos);
		if ( store )
			s.vis = vis;
		seen += s.lum * vis;
		total += s.lum;
	}
	return total > 0.0 ? (float)(seen / total) : 0.0f;
}

// cone: axis xyz, cos outer, cos inner (null: point)
inline float ModelShape( const LightSample& s, float R, const float *cone )
{
	float a = PointAttenuation(s.r, R);
	if ( cone )
		a *= Smoothstep(cone[3], cone[4], Dot(s.dirOut, Make(cone[0], cone[1], cone[2])));
	return a;
}

// weighted percentile of (value, weight) pairs
float WeightedPercentile( std::vector<std::pair<float, float>>& vw, float p )
{
	if ( vw.empty() )
		return 0.0f;
	std::sort(vw.begin(), vw.end());
	double total = 0.0;
	for ( const auto& e : vw )
		total += e.second;
	double sum = 0.0;
	for ( const auto& e : vw )
	{
		sum += e.second;
		if ( sum >= p * total )
			return e.first;
	}
	return vw.back().first;
}

// the color for a radius and its error, one sided: the runtime light must reproduce as much
// of the attributed light as it can without exceeding it (too little is safe, the rest stays
// baked; too much is excess light in the fog). The brightness is a low weighted percentile of
// L / A over the probes it reaches (a lower envelope), the quality the explained part minus
// twice the overshoot. q3map lights fall off as 1 / r^2 while the runtime attenuation saturates
// at 1 inside R / sqrt(3): saturated probes say nothing about the shape and are left out. A point
// model across a spot's dark sector must stay dim, so the cone wins there.
float FitError( const std::vector<LightSample>& samples, float R, const float *cone, float *color, float *quality )
{
	std::vector<std::pair<float, float>> ratios;
	double chroma[3] = { 0.0, 0.0, 0.0 }, chromaLum = 0.0;
	for ( const LightSample& s : samples )
	{
		if ( s.vis <= 0.5f )
			continue;
		const float a = ModelShape(s, R, cone);
		if ( a < MODEL_MIN || a >= 0.999f )
			continue;
		ratios.push_back(std::make_pair(s.lum / a, a));
		for ( int c = 0; c < 3; c++ )
			chroma[c] += a * s.L[c];
		chromaLum += a * s.lum;
	}
	color[0] = color[1] = color[2] = 0.0f;
	*quality = 0.0f;
	if ( (int)ratios.size() < FIT_MIN_PROBES || chromaLum <= 0.0 )
		return 1e30f;
	const float brightness = WeightedPercentile(ratios, FIT_ENVELOPE);
	for ( int c = 0; c < 3; c++ )
		color[c] = (float)(brightness * chroma[c] / chromaLum);

	double explained = 0.0, over = 0.0, total = 0.0;
	for ( const LightSample& s : samples )
	{
		if ( s.vis <= 0.5f )
			continue;
		const float a = ModelShape(s, R, cone);
		if ( a < MODEL_MIN || a >= 0.999f )
			continue;
		const float model = brightness * a;
		explained += std::min(model, s.lum);
		over += std::max(model - s.lum, 0.0f);
		total += s.lum;
	}
	if ( total <= 0.0 )
		return 1e30f;
	*quality = Saturate((float)((explained - 2.0 * over) / total));
	return 1.0f - *quality;
}

// the radius by a log spaced search and a golden section refinement
RuntimeFit FitRuntime( const std::vector<LightSample>& samples, float rangeHint, const float *cone )
{
	float radii[LIGHT_RADIUS_STEPS], errors[LIGHT_RADIUS_STEPS];
	float color[3], rel;
	int bestK = 0;
	for ( int k = 0; k < LIGHT_RADIUS_STEPS; k++ )
	{
		const float t = (float)k / (LIGHT_RADIUS_STEPS - 1);
		radii[k] = rangeHint * LIGHT_RADIUS_MIN * powf(LIGHT_RADIUS_MAX / LIGHT_RADIUS_MIN, t);
		errors[k] = FitError(samples, radii[k], cone, color, &rel);
		if ( errors[k] < errors[bestK] )
			bestK = k;
	}
	float lo = radii[std::max(bestK - 1, 0)], hi = radii[std::min(bestK + 1, LIGHT_RADIUS_STEPS - 1)];
	const float phi = 0.6180340f;
	float x1 = hi - phi * (hi - lo), x2 = lo + phi * (hi - lo);
	float f1 = FitError(samples, x1, cone, color, &rel), f2 = FitError(samples, x2, cone, color, &rel);
	for ( int it = 0; it < 12; it++ )
	{
		if ( f1 < f2 )
		{
			hi = x2; x2 = x1; f2 = f1;
			x1 = hi - phi * (hi - lo);
			f1 = FitError(samples, x1, cone, color, &rel);
		}
		else
		{
			lo = x1; x1 = x2; f1 = f2;
			x2 = lo + phi * (hi - lo);
			f2 = FitError(samples, x2, cone, color, &rel);
		}
	}
	RuntimeFit fit;
	fit.radius = std::min(f1, f2) <= errors[bestK] ? (f1 < f2 ? x1 : x2) : radii[bestK];
	fit.error = FitError(samples, fit.radius, cone, fit.color, &fit.quality);
	return fit;
}

// the attributed part L_ij of one source into ctx.scratchL (its cells in touched), recomputed
// from the finished accumulators instead of stored per source; ctx.domain is its domain afterwards
void SourceContributions( Ctx& ctx, const Source& src, const std::vector<Accumulator>& acc, std::vector<int>& touched )
{
	touched.clear();
	MatchSource(ctx, src, [&]( int i, const float *m, V3 ) {
		const Accumulator& a = acc[i];
		float *l = &ctx.scratchL[i * 3];
		for ( int c = 0; c < 3; c++ )
			if ( m[c] > 0.0f && a.sumM[c] > 0.0f )
				l[c] = ctx.Q[i * 3 + c] * a.maxM[c] * m[c] / a.sumM[c];
		touched.push_back(i);
	});
}

// distance bins x octants around the origin; per bin the brightest probe and evenly spaced
// others, the dark ones too (they are the evidence of the radius and of a cone)
void SelectSamples( Ctx& ctx, V3 origin, float range, std::vector<LightSample>& samples )
{
	samples.clear();
	const int numBins = LIGHT_DISTANCE_BINS * 8;
	std::vector<int> bins[LIGHT_DISTANCE_BINS * 8];
	for ( int i : ctx.domain )
	{
		float r;
		const V3 d = Normalize(Sub(ctx.Position(i), origin), &r);
		const int db = std::min(LIGHT_DISTANCE_BINS - 1, (int)(r / std::max(range, 1e-3f) * LIGHT_DISTANCE_BINS));
		const int oct = (d.x >= 0.0f ? 1 : 0) | (d.y >= 0.0f ? 2 : 0) | (d.z >= 0.0f ? 4 : 0);
		bins[db * 8 + oct].push_back(i);
	}
	for ( int b = 0; b < numBins; b++ )
	{
		const std::vector<int>& list = bins[b];
		if ( list.empty() )
			continue;
		int brightest = list[0];
		for ( int i : list )
			if ( Luma(&ctx.scratchL[i * 3]) > Luma(&ctx.scratchL[brightest * 3]) )
				brightest = i;
		int picked[LIGHT_SAMPLES_PER_BIN];
		int count = 0;
		picked[count++] = brightest;
		const int others = std::min((int)list.size(), LIGHT_SAMPLES_PER_BIN - 1);
		for ( int k = 0; k < others; k++ )
		{
			const int i = list[(k * list.size() + list.size() / 2) / others];
			if ( i != brightest )
				picked[count++] = i;
		}
		for ( int k = 0; k < count; k++ )
		{
			LightSample s;
			s.cell = picked[k];
			s.pos = ctx.Position(s.cell);
			for ( int c = 0; c < 3; c++ )
				s.L[c] = ctx.scratchL[s.cell * 3 + c];
			s.lum = Luma(s.L);
			s.vis = 0.0f;
			samples.push_back(s);
		}
	}
	SetOrigin(samples, origin);
}

// validate and fit one point proxy; false if it cannot be fitted at all (a transport proxy)
bool ClassifyPoint( Ctx& ctx, const Source& src, const std::vector<Accumulator>& acc,
	std::vector<int>& touched, std::vector<LightSample>& samples, vrStaticLight& light )
{
	const vrProxy& proxy = src.proxy;
	SourceContributions(ctx, src, acc, touched);
	V3 origin = src.position;
	SelectSamples(ctx, origin, proxy.range, samples);

	double explained = 0.0, animated = 0.0;
	for ( int i : touched )
	{
		const float lum = Luma(&ctx.scratchL[i * 3]);
		explained += lum;
		if ( ctx.in->animated && ctx.in->animated[i] )
			animated += lum;
		ctx.scratchL[i * 3] = ctx.scratchL[i * 3 + 1] = ctx.scratchL[i * 3 + 2] = 0.0f;
	}
	light.explainedEnergy = (float)explained;
	// a flickering lamp is no static light
	if ( explained > 0.0 && animated > 0.5 * explained )
		light.flags |= VR_LIGHT_ANIMATED;
	if ( samples.size() < 8 || explained <= 0.0 )
		return false;

	// visibility, and relocation within the positional uncertainty (never beyond it: the fit
	// must not invent a lamp)
	float V = SampleVisibility(ctx, samples, origin, true);
	if ( V < RELOCATE_VISIBILITY && proxy.sigmaP > 0.0f )
	{
		const float rad = std::min(proxy.sigmaP, RELOCATE_MAX * ctx.diag);
		const float s2 = proxy.sigmaP * proxy.sigmaP;
		float bestScore = V;
		V3 best = origin;
		for ( int ring = 1; ring <= 2; ring++ )
		{
			const float step = rad * (ring == 1 ? 0.5f : 1.0f);
			for ( int z = -1; z <= 1; z++ )
				for ( int y = -1; y <= 1; y++ )
					for ( int x = -1; x <= 1; x++ )
					{
						if ( !x && !y && !z )
							continue;
						const V3 o = Scale(Normalize(Make((float)x, (float)y, (float)z)), step);
						const V3 c = Add(src.position, o);
						const float score = SampleVisibility(ctx, samples, c, false) * expf(-0.5f * Dot(o, o) / s2);
						if ( score > bestScore + 0.02f )
						{
							bestScore = score;
							best = c;
						}
					}
		}
		if ( best.x != origin.x || best.y != origin.y || best.z != origin.z )
		{
			origin = best;
			light.flags |= VR_LIGHT_RELOCATED;
			SetOrigin(samples, origin);
			V = SampleVisibility(ctx, samples, origin, true);
		}
	}
	light.origin[0] = origin.x; light.origin[1] = origin.y; light.origin[2] = origin.z;
	light.visibility = V;

	// point model
	const RuntimeFit point = FitRuntime(samples, proxy.range, nullptr);
	light.kind = VR_LIGHT_POINT;
	light.radius = point.radius;
	for ( int c = 0; c < 3; c++ )
		light.color[c] = point.color[c];
	light.fitError = point.error;
	light.radiometricConfidence = point.quality;
	// the visibility decides; the transport confidence (a doorway converges as well as a lamp)
	// and the position only temper the model quality
	const auto physical = [&]( const RuntimeFit& fit ) {
		const float R2 = fit.radius * fit.radius;
		const float cPosition = R2 / (R2 + 4.0f * proxy.sigmaP * proxy.sigmaP);
		return V * sqrtf(fit.quality * cPosition) * sqrtf(sqrtf(proxy.confidence));
	};
	light.physicalConfidence = physical(point);
	const float physicalThreshold = ctx.in->physicalThreshold > 0.0f ? ctx.in->physicalThreshold : PHYSICAL_THRESHOLD;

	// a spot: decided before the physical test, a point model of a spot explains nothing
	do
	{
		// spot: concentrated outgoing directions of the visible lit probes, weighted by the angular
		// intensity lum * r^2 (by lum alone the nearest probes decide, all around the light)
		V3 m = Make(0.0f, 0.0f, 0.0f), nearAxis = m, farAxis = m;
		double wsum = 0.0;
		std::vector<float> litR;
		for ( const LightSample& s : samples )
			if ( s.vis > 0.5f && s.lum > 0.0f )
			{
				const float w = s.lum * s.r * s.r;
				m = Add(m, Scale(s.dirOut, w));
				wsum += w;
				litR.push_back(s.r);
			}
		if ( wsum <= 0.0 || litR.size() < 6 )
			break;
		float mLength;
		const V3 axis = Normalize(m, &mLength);
		const float rho = (float)(mLength / wsum);
		if ( rho < SPOT_CONCENTRATION )
			break;

		// the same axis from the near and the far half
		std::nth_element(litR.begin(), litR.begin() + litR.size() / 2, litR.end());
		const float medianR = litR[litR.size() / 2];
		std::vector<std::pair<float, float>> thetas;
		for ( const LightSample& s : samples )
			if ( s.vis > 0.5f && s.lum > 0.0f )
			{
				const float w = s.lum * s.r * s.r;
				if ( s.r <= medianR )
					nearAxis = Add(nearAxis, Scale(s.dirOut, w));
				else
					farAxis = Add(farAxis, Scale(s.dirOut, w));
				thetas.push_back(std::make_pair(acosf(std::max(-1.0f, std::min(1.0f, Dot(s.dirOut, axis)))), w));
			}
		if ( Dot(Normalize(nearAxis), Normalize(farAxis)) < SPOT_AXIS_COS )
			break;

		// the cone: the seed fit of a spot lands inside its cone, below the apex, so the origin slides
		// along the axis within the positional uncertainty (keeping its visibility). Per origin the
		// outer angle around the 92nd percentile of the lit directions, the inner as a fraction of it.
		const float degree = 0.0174533f;
		const float outerSteps[5] = { -10.0f, -5.0f, 0.0f, 5.0f, 10.0f };
		const float innerFractions[4] = { 0.4f, 0.6f, 0.75f, 0.9f };
		const float slide = std::min(1.5f * proxy.sigmaP, 1.5f * ctx.diag);
		RuntimeFit spot = RuntimeFit();
		spot.error = 1e30f;
		float cone[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
		V3 apex = origin;
		std::vector<LightSample> moved = samples, best = samples;
		for ( int k = -4; k <= 4; k++ )
		{
			const V3 c = Add(origin, Scale(axis, slide * k / 4.0f));
			if ( k )
			{
				SetOrigin(moved, c);
				if ( SampleVisibility(ctx, moved, c, true) < V - 0.05f )
					continue;
			}
			else
				moved = samples;
			thetas.clear();
			for ( const LightSample& s : moved )
				if ( s.vis > 0.5f && s.lum > 0.0f )
					thetas.push_back(std::make_pair(acosf(std::max(-1.0f, std::min(1.0f, Dot(s.dirOut, axis)))), s.lum * s.r * s.r));
			const float outer0 = WeightedPercentile(thetas, 0.92f);
			for ( float os : outerSteps )
			{
				const float outer = std::min(1.48f, std::max(SPOT_OUTER_MIN, outer0 + os * degree));
				for ( float f : innerFractions )
				{
					const float candidate[5] = { axis.x, axis.y, axis.z, cosf(outer), cosf(outer * f) };
					const RuntimeFit fit = FitRuntime(moved, proxy.range, candidate);
					if ( fit.error < spot.error )
					{
						spot = fit;
						memcpy(cone, candidate, sizeof(cone));
						apex = c;
						best = moved;
					}
				}
			}
		}
		const float improvement = point.error > 0.0f ? 1.0f - spot.error / point.error : 0.0f;
		if ( improvement < SPOT_IMPROVEMENT )
			break;

		// the dark sector must be open space: probes outside the cone that the light sees. Behind
		// a doorway the dark sector is blocked, and the light stays a point.
		int outside = 0, openOutside = 0;
		for ( const LightSample& s : best )
			if ( Dot(s.dirOut, axis) < cone[3] )
			{
				outside++;
				if ( s.vis > 0.5f )
					openOutside++;
			}
		const float open = outside >= 4 ? (float)openOutside / outside : 0.0f;
		const float spotPhysical = physical(spot);
		light.spotConfidence = spotPhysical * Saturate((rho - SPOT_CONCENTRATION) / 0.3f) *
			Saturate(improvement / 0.5f) * Saturate((open - 0.25f) / 0.5f);
		const float spotThreshold = ctx.in->spotThreshold > 0.0f ? ctx.in->spotThreshold : SPOT_THRESHOLD;
		if ( light.spotConfidence < spotThreshold * physicalThreshold )
			break;

		if ( apex.x != origin.x || apex.y != origin.y || apex.z != origin.z )
		{
			light.flags |= VR_LIGHT_RELOCATED;
			light.origin[0] = apex.x; light.origin[1] = apex.y; light.origin[2] = apex.z;
		}

		light.kind = VR_LIGHT_SPOT;
		light.radius = spot.radius;
		for ( int c = 0; c < 3; c++ )
			light.color[c] = spot.color[c];
		light.fitError = spot.error;
		light.radiometricConfidence = spot.quality;
		light.physicalConfidence = spotPhysical;
		light.axis[0] = axis.x; light.axis[1] = axis.y; light.axis[2] = axis.z;
		light.cosOuter = cone[3];
		light.cosInner = cone[4];
	} while ( 0 );

	if ( light.physicalConfidence >= physicalThreshold && MaxComponent(Load(light.color)) > 0.0f )
		light.flags |= VR_LIGHT_PHYSICAL;
	return true;
}

// the light of a structured light at x (point / spot, its color as stored)
inline void LightModel( const vrStaticLight& l, V3 x, float *out )
{
	float r;
	const V3 d = Normalize(Sub(x, Load(l.origin)), &r);
	float a = PointAttenuation(r, l.radius);
	if ( l.kind == VR_LIGHT_SPOT )
		a *= Smoothstep(l.cosOuter, l.cosInner, Dot(d, Load(l.axis)));
	for ( int c = 0; c < 3; c++ )
		out[c] = l.color[c] * a;
}

void BuildLights( Ctx& ctx, const std::vector<Source>& sources, const std::vector<Accumulator>& acc,
	std::vector<bool>& promoted, vrOutput& out )
{
	vrStats& st = out.stats;
	promoted.assign(sources.size(), false);
	out.lights.clear();
	out.promoted.clear();

	std::vector<int> touched;
	std::vector<LightSample> samples;
	if ( ctx.in->trace )
		ctx.scratchL.assign(ctx.n * 3, 0.0f);
	for ( size_t k = 0; k < sources.size(); k++ )
	{
		const Source& src = sources[k];
		vrStaticLight light;
		memset(&light, 0, sizeof(light));
		light.proxy = (int)k;
		light.confidence = src.proxy.confidence;
		light.sigmaP = src.proxy.sigmaP;
		light.radius = src.proxy.range;
		light.explainedEnergy = src.energy;
		for ( int c = 0; c < 3; c++ )
		{
			light.origin[c] = src.proxy.position[c];
			light.color[c] = src.proxy.color[c];
		}
		light.cosInner = -1.0f;
		light.cosOuter = -2.0f;
		if ( src.proxy.area >= 0 )
		{
			// known emitters: not promoted yet (grid calibrated area lights come later)
			light.kind = VR_LIGHT_RECT;
			for ( int c = 0; c < 3; c++ )
			{
				light.right[c] = src.proxy.right[c];
				light.up[c] = src.proxy.up[c];
			}
			light.halfWidth = src.proxy.halfWidth;
			light.halfHeight = src.proxy.halfHeight;
			light.twoSided = src.proxy.twoSided;
		}
		else
		{
			if ( !ctx.in->trace || !ClassifyPoint(ctx, src, acc, touched, samples, light) ||
				light.physicalConfidence < TRANSPORT_THRESHOLD )
				light.kind = VR_LIGHT_TRANSPORT;
			if ( light.flags & VR_LIGHT_PHYSICAL )
				st.physicalLights++;
			if ( light.kind == VR_LIGHT_TRANSPORT )
				st.transportLights++;
			if ( light.flags & VR_LIGHT_RELOCATED )
				st.relocatedLights++;
		}
		out.lights.push_back(light);
	}
	std::vector<float>().swap(ctx.scratchL);
	st.traces = ctx.traces;
	if ( !ctx.in->trace || !ctx.in->promote )
		return;

	// promotion, strongest first: the modelled light at the probes the light sees leaves Q and B
	std::vector<int> order;
	for ( size_t k = 0; k < out.lights.size(); k++ )
	{
		const vrStaticLight& l = out.lights[k];
		if ( (l.flags & VR_LIGHT_PHYSICAL) && !(l.flags & VR_LIGHT_ANIMATED) )
			order.push_back((int)k);
	}
	std::stable_sort(order.begin(), order.end(), [&]( int a, int b ) {
		const vrStaticLight& la = out.lights[a];
		const vrStaticLight& lb = out.lights[b];
		return la.confidence * la.explainedEnergy * la.physicalConfidence >
			lb.confidence * lb.explainedEnergy * lb.physicalConfidence; });
	const int maxPromoted = ctx.in->maxPromoted > 0 ? ctx.in->maxPromoted : PROMOTED_MAX;

	double baselineSum = 0.0;
	for ( int i = 0; i < ctx.n; i++ )
		if ( ctx.Valid(i) )
			baselineSum += Luma(ctx.B + i * 3);
	const std::vector<float> original(out.baseline);
	std::vector<float> P(ctx.n * 3, 0.0f);
	double promotedSum = 0.0, excessSum = 0.0;
	struct Hit
	{
		int cell;
		float model[3];
	};
	std::vector<Hit> hits;
	int numPromoted = 0;
	for ( int k : order )
	{
		if ( numPromoted >= maxPromoted )
			break;
		vrStaticLight& l = out.lights[k];
		// partial promotion: an uncertain light leaves the rest of its energy baked (the model
		// itself is already a lower envelope of the attributed light)
		const float w = Saturate(l.physicalConfidence);
		if ( w <= 0.0f )
			continue;
		const V3 o = Load(l.origin);
		const float R2 = l.radius * l.radius;
		const int start = NearestValid(ctx, o);
		if ( start < 0 )
			continue;
		Flood(ctx, start, [&]( int j ) {
			const V3 d = Sub(ctx.Position(j), o);
			return Dot(d, d) <= R2;
		}, DOMAIN_MAX, ctx.domain);

		vrStaticLight scaled = l;
		for ( int c = 0; c < 3; c++ )
			scaled.color[c] = l.color[c] * w;
		// the runtime light has no shadow (yet): too much of it behind walls and it stays baked
		hits.clear();
		double seen = 0.0, leak = 0.0, over = 0.0;
		for ( int i : ctx.domain )
		{
			Hit h;
			h.cell = i;
			LightModel(scaled, ctx.Position(i), h.model);
			const float lum = Luma(h.model);
			if ( lum <= 0.0f )
				continue;
			if ( Trace(ctx, o, ctx.Position(i)) > 0.5f )
			{
				seen += lum;
				for ( int c = 0; c < 3; c++ )
					over += LUMA[c] * std::max(h.model[c] - ctx.Q[i * 3 + c], 0.0f);
				hits.push_back(h);
			}
			else
				leak += lum;
		}
		l.leakFraction = seen + leak > 0.0 ? (float)(leak / (seen + leak)) : 1.0f;
		// and the runtime light must not be brighter than the baked light it replaces
		l.excessFraction = seen > 0.0 ? (float)(over / seen) : 1.0f;
		if ( l.leakFraction > LEAK_MAX || l.excessFraction > EXCESS_MAX || seen <= 0.0 )
			continue;

		for ( const Hit& h : hits )
			for ( int c = 0; c < 3; c++ )
			{
				const int idx = h.cell * 3 + c;
				const float add = std::min(h.model[c], ctx.Q[idx]);
				ctx.Q[idx] -= add;
				out.baseline[idx] -= add;
				P[idx] += add;
				promotedSum += LUMA[c] * add;
				excessSum += LUMA[c] * (h.model[c] - add);
			}
		l.promotionWeight = w;
		for ( int c = 0; c < 3; c++ )
			l.color[c] = scaled.color[c];
		l.flags |= VR_LIGHT_PROMOTED;
		promoted[l.proxy] = true;
		numPromoted++;
		if ( l.kind == VR_LIGHT_SPOT )
			st.promotedSpots++;
		else
			st.promotedPoints++;
	}
	st.traces = ctx.traces;
	if ( !numPromoted )
		return;

	out.promoted.swap(P);
	for ( size_t i = 0; i < original.size(); i++ )
		st.maxPartitionError = std::max(st.maxPartitionError, fabsf(out.baseline[i] + out.promoted[i] - original[i]));
	st.promotedFraction = baselineSum > 0.0 ? (float)(promotedSum / baselineSum) : 0.0f;
	st.excessFraction = promotedSum > 0.0 ? (float)(excessSum / promotedSum) : 0.0f;
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

	// merge fits of one light: close and of the same colour (two white lamps apart stay two).
	// The strongest fit is kept whole (position, profile, sigma and start cell stay one
	// consistent model); a weaker duplicate only adds its energy and its reach.
	std::stable_sort(fitted.begin(), fitted.end(), []( const Source& a, const Source& b ) {
		return a.proxy.confidence * a.energy > b.proxy.confidence * b.energy; });
	std::vector<Source> points;
	for ( const Source& s : fitted )
	{
		bool merged = false;
		for ( size_t k = 0; k < points.size(); k++ )
		{
			Source& o = points[k];
			// overlapping uncertainty: one light fitted twice from different seeds
			const float distance = std::min(MERGE_DISTANCE_MAX * ctx.diag,
				std::max(MERGE_DISTANCE * ctx.diag, o.proxy.sigmaP + s.proxy.sigmaP));
			if ( Length(Sub(o.position, s.position)) < distance && Dot(o.chroma, s.chroma) > MERGE_CHROMA_COS )
			{
				o.proxy.range = std::max(o.proxy.range, s.proxy.range);
				o.energy += s.energy;
				st.mergedFits++;
				merged = true;
				break;
			}
		}
		if ( !merged )
			points.push_back(s);
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
	// one budget for all sources: anchors (known emitters) first, points fill the rest
	if ( (int)anchors.size() > PROXY_MAX )
	{
		st.droppedSources += (int)anchors.size() - PROXY_MAX;
		anchors.resize(PROXY_MAX);
	}
	const int pointBudget = PROXY_MAX - (int)anchors.size();
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
		if ( duplicate )
			continue;
		if ( (int)sources.size() < pointBudget )
			sources.push_back(p);
		else
			st.droppedSources++;
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

	st.attributedFraction = (float)ResolveMoments(ctx, acc, out);
	st.msecAttribution = Msec(t);

	for ( const Source& s : sources )
		out.proxies.push_back(s.proxy);

	// structured lights; the promoted ones leave B / Q, the moments are rebuilt from the rest
	t = Clock::now();
	std::vector<bool> promoted;
	BuildLights(ctx, sources, acc, promoted, out);
	if ( !out.promoted.empty() )
	{
		memset(acc.data(), 0, acc.size() * sizeof(Accumulator));
		for ( size_t k = 0; k < sources.size(); k++ )
			if ( !promoted[k] )
				Attribute(ctx, sources[k], acc);
		st.attributedFraction = (float)ResolveMoments(ctx, acc, out);
	}
	st.msecLights = Msec(t);
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
	ctx.traces = 0;

	const int n = ctx.n;
	out.baseline.assign(n * 3, 0.0f);
	out.sun.assign(n * 3, 0.0f);
	out.sunFraction.assign(n, 0.0f);
	for ( int c = 0; c < 3; c++ )
		out.moment[c].clear();
	out.proxies.clear();
	out.lights.clear();
	out.promoted.clear();
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
		const bool valid = in.valid[i] != 0;
		// wall cells have no meaningful BSP direction: they stay isotropic static light
		const float f = valid ? align * vis : 0.0f;
		out.sunFraction[i] = f;
		if ( valid )
			st.validCells++;
		for ( int c = 0; c < 3; c++ )
		{
			const float a = std::max(A[c], 0.0f), d = std::max(D[c], 0.0f);
			const float legacy = in.hdr ? a + d : std::max(a, d);
			const float s = std::min(std::max(f * d, 0.0f), legacy);
			const float b = legacy - s;
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
