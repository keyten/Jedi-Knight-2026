/*[Fragment]*/
// Liquid media (r_volumetricWater), tr_liquid.cpp, docs/rend2-volumetric-fog.md "Underwater medium".
//
// This file is not a program on its own: its fragment block follows volumetric_common.glsl in
// volumetric_inject / volumetric_debug (the medium, debug views 59-64) and is the fragment library
// of the lit lightall permutations with a sun (USE_LIQUID_SUN: the underwater sun). It does not
// use the froxel functions. Everything is inside USE_LIQUIDS.
//
// A liquid brush is convex: its inside is dot(n, p) - d <= 0 for all its planes (u_LiquidPlanes,
// first plane u_LiquidMins[i].w, int(u_LiquidMaxs[i].w) & 63 planes; the first six are the axial
// bounds, also in u_LiquidMins / u_LiquidMaxs). A ray o + t dir is clipped exactly: every plane
// narrows [enter, exit]. The brushes of a map overlap (they are the mapper's brushes, not split by
// the BSP): covered lengths are unions of the intervals of a medium, never sums.
//
// Class and medium: a brush has the liquid class of its contents (what gameplay sees, debug view 61)
// and a medium slot, the optics it takes (u_LiquidMaterial: water, slime, lava); a water brush under a
// slime-flagged surface or an env.json rule has another medium than its class (tr_liquid.cpp).
// Overlapping brushes of different media add up (no stock map has any).

#if defined(USE_LIQUIDS)
layout(std140) uniform Liquids
{
	vec4 u_LiquidParams;		// visible brushes, camera liquid class (-1 none), sun path on, class mask
	vec4 u_LiquidCaustics;		// 1 / period (world units), animation phase, focus depth, strength (0 = off)
	vec4 u_LiquidView;			// fade out start (view depth), 1 / fade length, froxel size per unit of view depth, unused
	vec4 u_LiquidMaterial[6];	// per medium: (extinction color rgb (mean 1), a: extinction per unit), (albedo rgb, a: g)
	vec4 u_LiquidMins[MAX_GPU_LIQUIDS];	// bounds, w: first plane
	vec4 u_LiquidMaxs[MAX_GPU_LIQUIDS];	// w: planes + 64 * medium + 256 * class
	ivec4 u_LiquidSlices[FROXEL_MAX_SLICES / 4];	// per froxel slice: bit i = brush i may touch it
};

uniform samplerBuffer u_LiquidPlanes;
uniform sampler2D u_LiquidCausticMap;	// tiling sun caustic pattern, mean 1 (mips too)

#define LIQUID_WATER 0
#define LIQUID_MAX_HITS 8

int LiquidCount()
{
	return int(u_LiquidParams.x);
}

// gameplay class of the brush i (its contents)
int LiquidClassOf(in int i)
{
	return int(u_LiquidMaxs[i].w) >> 8;
}

// medium (optics slot) of the brush i
int LiquidMediumOf(in int i)
{
	return (int(u_LiquidMaxs[i].w) >> 6) & 3;
}

// [enter, exit] of the ray o + t * dir inside the brush i, within [t0, t1]; enter >= exit: missed
vec2 LiquidClip(in int i, in vec3 o, in vec3 dir, in float t0, in float t1)
{
	// the axial bounds (the first six planes) as a slab test
	vec3 safeDir = mix(dir, vec3(1e-8), lessThan(abs(dir), vec3(1e-8)));
	vec3 inv = 1.0 / safeDir;
	vec3 ta = (u_LiquidMins[i].xyz - o) * inv;
	vec3 tb = (u_LiquidMaxs[i].xyz - o) * inv;
	vec3 lo = min(ta, tb);
	vec3 hi = max(ta, tb);
	float enter = max(t0, max(lo.x, max(lo.y, lo.z)));
	float exit = min(t1, min(hi.x, min(hi.y, hi.z)));
	if (enter >= exit)
		return vec2(1.0, 0.0);

	int first = int(u_LiquidMins[i].w);
	int count = int(u_LiquidMaxs[i].w) & 63;
	for (int k = 6; k < count; k++)
	{
		vec4 plane = texelFetch(u_LiquidPlanes, first + k);
		float denom = dot(plane.xyz, dir);
		float dist = dot(plane.xyz, o) - plane.w;
		if (abs(denom) < 1e-8)
		{
			if (dist > 0.0)
				return vec2(1.0, 0.0);	// parallel, outside
			continue;
		}
		float t = -dist / denom;
		if (denom < 0.0)
			enter = max(enter, t);
		else
			exit = min(exit, t);
		if (enter >= exit)
			return vec2(1.0, 0.0);
	}
	return vec2(enter, exit);
}

// p inside the brush i, grown by eps world units
bool LiquidInside(in int i, in vec3 p, in float eps)
{
	if (any(lessThan(p, u_LiquidMins[i].xyz - vec3(eps))) || any(greaterThan(p, u_LiquidMaxs[i].xyz + vec3(eps))))
		return false;
	int first = int(u_LiquidMins[i].w);
	int count = int(u_LiquidMaxs[i].w) & 63;
	for (int k = 6; k < count; k++)
	{
		vec4 plane = texelFetch(u_LiquidPlanes, first + k);
		if (dot(plane.xyz, p) - plane.w > eps)
			return false;
	}
	return true;
}

// liquid class at p from the GPU brushes (debug view 61), -1 = none
int LiquidPointClass(in vec3 p)
{
	int n = LiquidCount();
	for (int i = 0; i < n; i++)
	{
		if (LiquidInside(i, p, 0.0))
			return LiquidClassOf(i);
	}
	return -1;
}

// planes of the brush i within width of p (an edge of the brush where two or more are)
int LiquidNearPlanes(in int i, in vec3 p, in float width)
{
	int first = int(u_LiquidMins[i].w);
	int count = int(u_LiquidMaxs[i].w) & 63;
	int near = 0;
	for (int k = 0; k < count; k++)
	{
		vec4 plane = texelFetch(u_LiquidPlanes, first + k);
		if (abs(dot(plane.xyz, p) - plane.w) < width)
			near++;
	}
	return near;
}

// the intervals of a ray inside liquid brushes, sorted by enter: (enter, exit, medium). The
// LIQUID_MAX_HITS earliest are kept; the lengths of the later ones go to overflow (summed, may count
// overlaps twice; never seen with the stock maps: the froxel segments are short, the slices have few
// brushes, a sun ray from inside a stock liquid crosses at most a few).
struct LiquidHits
{
	vec3 seg[LIQUID_MAX_HITS];
	int count;
	vec3 overflow;
};

void LiquidHitsInit(out LiquidHits h)
{
	for (int k = 0; k < LIQUID_MAX_HITS; k++)
		h.seg[k] = vec3(0.0);
	h.count = 0;
	h.overflow = vec3(0.0);
}

void LiquidAddHit(inout LiquidHits h, in vec2 interval, in int medium)
{
	int k = h.count;
	if (k >= LIQUID_MAX_HITS)
	{
		// full: keep the earliest (the sun path is a run from p, a prefix), the latest to overflow
		vec3 last = h.seg[LIQUID_MAX_HITS - 1];
		if (interval.x >= last.x)
		{
			h.overflow[medium] += interval.y - interval.x;
			return;
		}
		h.overflow[int(last.z + 0.5)] += last.y - last.x;
		k = LIQUID_MAX_HITS - 1;
		h.count = k;
	}
	while (k > 0 && h.seg[k - 1].x > interval.x)
	{
		h.seg[k] = h.seg[k - 1];
		k--;
	}
	h.seg[k] = vec3(interval, float(medium));
	h.count++;
}

// covered length per medium (water, slime, lava): the union of the sorted intervals of a medium
vec3 LiquidUnion(in LiquidHits h)
{
	vec3 len = h.overflow;
	vec3 end = vec3(-1e30);
	for (int k = 0; k < h.count; k++)
	{
		vec3 s = h.seg[k];
		int c = int(s.z + 0.5);
		float e = end[c];
		if (s.y > e)
		{
			len[c] += s.y - max(s.x, e);
			end[c] = s.y;
		}
	}
	return len;
}

// length of the ray o + t * dir, t in [t0, t1], inside the liquids of the brushes in mask (bit i =
// brush i, -1 = all), per medium: the sorted list of LiquidHits, for segments where a medium has
// three or more intervals
vec3 LiquidCoverageSorted(in vec3 o, in vec3 dir, in float t0, in float t1, in int mask)
{
	LiquidHits h;
	LiquidHitsInit(h);
	int n = LiquidCount();
	for (int i = 0; i < n; i++)
	{
		if (((mask >> i) & 1) == 0)
			continue;
		vec2 interval = LiquidClip(i, o, dir, t0, t1);
		if (interval.x < interval.y)
			LiquidAddHit(h, interval, LiquidMediumOf(i));
	}
	return LiquidUnion(h);
}

// The same, exact, in registers while no medium has more than two intervals (nearly every froxel:
// one brush, or the seam of two touching / doubled brushes); the dynamically indexed hit list costs
// scratch memory on some GPUs (Intel UHD: ~20x the cost of the whole function) and runs only then.
vec3 LiquidCoverage(in vec3 o, in vec3 dir, in float t0, in float t1, in int mask)
{
	vec3 len = vec3(0.0);
	vec3 firstEnter = vec3(0.0);
	vec3 firstExit = vec3(0.0);
	ivec3 hits = ivec3(0);
	bool many = false;
	int n = LiquidCount();
	for (int i = 0; i < n && !many; i++)
	{
		if (((mask >> i) & 1) == 0)
			continue;
		vec2 interval = LiquidClip(i, o, dir, t0, t1);
		if (interval.x >= interval.y)
			continue;
		int c = LiquidMediumOf(i);
		if (hits[c] == 0)
		{
			firstEnter[c] = interval.x;
			firstExit[c] = interval.y;
			len[c] = interval.y - interval.x;
		}
		else if (hits[c] == 1)
		{
			// the union of two intervals
			float overlap = max(min(interval.y, firstExit[c]) - max(interval.x, firstEnter[c]), 0.0);
			len[c] += interval.y - interval.x - overlap;
		}
		else
			many = true;
		hits[c]++;
	}
	if (many)
		return LiquidCoverageSorted(o, dir, t0, t1, mask);
	return len;
}

// optical depth of covered lengths per medium: rgb (relative extinction color, mean 1)
vec3 LiquidOpticalDepth(in vec3 covered)
{
	return covered.x * u_LiquidMaterial[0].a * u_LiquidMaterial[0].rgb +
		covered.y * u_LiquidMaterial[2].a * u_LiquidMaterial[2].rgb +
		covered.z * u_LiquidMaterial[4].a * u_LiquidMaterial[4].rgb;
}

// liquids end with the froxel volume (no tail term): fade out before far
float LiquidFade(in float viewDepth)
{
	float t = clamp((viewDepth - u_LiquidView.x) * u_LiquidView.y, 0.0, 1.0);
	return 1.0 - t * t * (3.0 - 2.0 * t);
}

// Sun caustics at the point where the sunlight entered the water (xy of the surface point),
// depth below it, footprint: world size of the receiver (pixel or froxel). Two drifting copies of
// the pattern (mean 1 each, so the mean stays 1); the contrast grows from 0 at the surface to full
// at the focus depth and slowly decays deeper. Box mips: big footprints see the mean.
float LiquidCaustic(in vec2 xy, in float depth, in float footprint)
{
	float strength = u_LiquidCaustics.w;
	if (strength <= 0.0)
		return 1.0;
	vec2 uv = xy * u_LiquidCaustics.x;
	float phase = u_LiquidCaustics.y;
	float lod = log2(max(footprint * u_LiquidCaustics.x * 256.0, 1.0));
	float a = textureLod(u_LiquidCausticMap, uv + vec2(0.71, 0.29) * phase, lod).r;
	float b = textureLod(u_LiquidCausticMap, uv * 1.37 + vec2(-0.43, 0.61) * phase + vec2(0.5), lod).r;
	float contrast = strength * smoothstep(0.0, u_LiquidCaustics.z, depth) / (1.0 + depth / 1024.0);
	return max(1.0 + contrast * (0.5 * (a + b) - 1.0), 0.0);
}

// The run of liquid that starts at p along a ray, from its intervals in enter order (enter, exit,
// medium): it ends at the first gap of air (more than 1 unit), lengths per medium are unions.
struct LiquidRun
{
	vec3 len;
	vec3 end;
	float runEnd;
	int top;		// medium of the interval that reaches farthest, -1: no run at p
	bool done;
};

void LiquidRunInit(out LiquidRun r)
{
	r.len = vec3(0.0);
	r.end = vec3(-1e30);
	r.runEnd = -1e30;
	r.top = -1;
	r.done = false;
}

void LiquidRunAdd(inout LiquidRun r, in vec3 s)
{
	if (r.done)
		return;
	if (s.x > max(r.runEnd, 0.0) + 1.0)
	{
		r.done = true;	// air between: the run ended (or never started at p)
		return;
	}
	if (s.y <= 0.0)
		return;	// behind p (within the 1 unit start tolerance)
	int c = int(s.z + 0.5);
	float a = max(s.x, 0.0);
	float e = r.end[c];
	if (s.y > e)
	{
		r.len[c] += s.y - max(a, e);
		r.end[c] = s.y;
	}
	if (s.y >= r.runEnd)
	{
		r.runEnd = s.y;
		r.top = c;
	}
}

// Sunlight reaching p inside a liquid (r_volumetricWaterSunPath): the rgb transmittance of the
// liquid between p and the surface along L (towards the sun, the unrefracted path), times the
// caustics where water is on top. The path is the connected run of liquid intervals that starts at
// p (brushes touching each other, within 1 unit); liquid beyond air above it is left to the
// geometry shadow, and so is a run longer than LIQUID_MAX_HITS intervals (the earliest are kept).
// pathLength: the liquid length of the run. 1 where p is in no liquid.
vec3 LiquidSunTransmittance(in vec3 p, in vec3 L, in float footprint, out float pathLength)
{
	pathLength = 0.0;
	int n = LiquidCount();
	if (n == 0 || u_LiquidParams.z < 0.5)
		return vec3(1.0);

	bool inside = false;
	for (int i = 0; i < n && !inside; i++)
		inside = LiquidInside(i, p, 1.0);
	if (!inside)
		return vec3(1.0);

	// the intervals along L: up to two in registers, more in the sorted hit list (see LiquidCoverage)
	LiquidRun run;
	LiquidRunInit(run);
	vec3 h0 = vec3(0.0), h1 = vec3(0.0);
	int count = 0;
	for (int i = 0; i < n && count <= 2; i++)
	{
		vec2 interval = LiquidClip(i, p, L, -1.0, 65536.0);
		if (interval.x >= interval.y)
			continue;
		vec3 s = vec3(interval, float(LiquidMediumOf(i)));
		if (count == 0)
			h0 = s;
		else if (count == 1)
			h1 = s;
		count++;
	}
	if (count <= 2)
	{
		if (count == 2 && h1.x < h0.x)
		{
			vec3 t = h0;
			h0 = h1;
			h1 = t;
		}
		if (count >= 1)
			LiquidRunAdd(run, h0);
		if (count == 2)
			LiquidRunAdd(run, h1);
	}
	else
	{
		LiquidHits h;
		LiquidHitsInit(h);
		for (int i = 0; i < n; i++)
		{
			vec2 interval = LiquidClip(i, p, L, -1.0, 65536.0);
			if (interval.x < interval.y)
				LiquidAddHit(h, interval, LiquidMediumOf(i));
		}
		for (int k = 0; k < h.count; k++)
			LiquidRunAdd(run, h.seg[k]);
	}
	vec3 len = run.len;
	float runEnd = run.runEnd;
	int topMedium = run.top;
	if (topMedium < 0)
		return vec3(1.0);

	pathLength = len.x + len.y + len.z;
	vec3 T = exp(-LiquidOpticalDepth(len));
	if (topMedium == LIQUID_WATER && L.z > 0.05)
		T *= LiquidCaustic((p + L * runEnd).xy, pathLength, footprint);
	return T;
}

vec3 LiquidClassHue(in int liquidClass)
{
	if (liquidClass == 0)
		return vec3(0.1, 0.45, 1.0);
	if (liquidClass == 1)
		return vec3(0.3, 1.0, 0.15);
	if (liquidClass == 2)
		return vec3(1.0, 0.35, 0.05);
	return vec3(0.15);
}
#endif
