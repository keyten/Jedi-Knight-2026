/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Cloud noise generator (r_clouds, tr_clouds.cpp, RB_CloudsGenerateNoise): one slice of a tiling 3D texture per
// draw, once per renderer start. Deterministic: lattice gradients and Worley feature points come from an integer
// hash (pcg3d) of the wrapped cell, every period divides the texture, so all textures tile in x, y and z.
//
// u_Cloud[0]: target (0 shape 128^3, 1 detail 32^3, 2 weather 512^2), slice, size (texels), 0
//   shape   r = Perlin-Worley (billowy Perlin fbm over a Worley fbm) eroded by a finer Worley fbm (Schneider 2015,
//             Hillaire 2016), precombined into one channel
//   detail  r = Worley fbm (erosion of the edges)
//   weather r = coverage (Perlin fbm, contrast stretched), g = cloud type (very low frequency Perlin)

uniform vec4 u_Cloud[20];

out vec4 out_Color;

uvec3 CloudHash3(uvec3 v)
{
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.z;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v ^= v >> 16u;
	v.x += v.y * v.z;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	return v;
}

vec3 CloudHash01(ivec3 cell, int period, uint seed)
{
	uvec3 c = uvec3((cell % period + period) % period);
	uvec3 h = CloudHash3(c + uvec3(seed, seed * 7u + 13u, seed * 31u + 101u));
	return vec3(h >> 8u) * (1.0 / 16777216.0);
}

vec3 CloudFade(vec3 t)
{
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// tiling gradient noise, period cells per unit, about [-1, 1]
float CloudPerlin(vec3 p, int period, uint seed)
{
	vec3 x = p * float(period);
	vec3 fl = floor(x);
	ivec3 i = ivec3(fl);
	vec3 f = x - fl;
	vec3 u = CloudFade(f);
	float corners[8];
	for (int k = 0; k < 8; k++)
	{
		ivec3 o = ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
		vec3 g = normalize(CloudHash01(i + o, period, seed) * 2.0 - 1.0 + vec3(1e-4));
		corners[k] = dot(g, f - vec3(o));
	}
	float x00 = mix(corners[0], corners[1], u.x);
	float x10 = mix(corners[2], corners[3], u.x);
	float x01 = mix(corners[4], corners[5], u.x);
	float x11 = mix(corners[6], corners[7], u.x);
	return mix(mix(x00, x10, u.y), mix(x01, x11, u.y), u.z) * 1.15;
}

// inverted tiling Worley F1: 1 at the feature points, 0 one cell away
float CloudWorley(vec3 p, int period, uint seed)
{
	vec3 x = p * float(period);
	vec3 fl = floor(x);
	ivec3 i = ivec3(fl);
	float d2 = 1e9;
	for (int k = 0; k < 27; k++)
	{
		ivec3 o = ivec3(k % 3, (k / 3) % 3, k / 9) - 1;
		ivec3 c = i + o;
		vec3 feature = vec3(c) + CloudHash01(c, period, seed);
		vec3 v = feature - x;
		d2 = min(d2, dot(v, v));
	}
	return 1.0 - clamp(sqrt(d2), 0.0, 1.0);
}

float CloudPerlinFbm(vec3 p, int period, int octaves, uint seed)
{
	float sum = 0.0;
	float amp = 1.0;
	float norm = 0.0;
	for (int o = 0; o < octaves; o++)
	{
		sum += CloudPerlin(p, period << o, seed + uint(o) * 1013u) * amp;
		norm += amp;
		amp *= 0.5;
	}
	return sum / norm;
}

float CloudWorleyFbm(vec3 p, int period, uint seed)
{
	return CloudWorley(p, period, seed) * 0.625 +
		CloudWorley(p, period * 2, seed + 17u) * 0.25 +
		CloudWorley(p, period * 4, seed + 41u) * 0.125;
}

float CloudRemap01(float x, float a, float b, float c, float d)
{
	return c + (x - a) * (d - c) / max(b - a, 1e-5);
}

void main()
{
	int target = int(u_Cloud[0].x);
	float size = u_Cloud[0].z;
	vec3 p = vec3(gl_FragCoord.xy, u_Cloud[0].y + 0.5) / size;

	if (target == 0)
	{
		// billowy Perlin fbm (|2n - 1| folded towards 1) dilated by a Worley fbm of the same scale
		float perlin = CloudPerlinFbm(p, 4, 3, 1u) * 0.5 + 0.5;
		perlin = abs(perlin * 2.0 - 1.0);
		perlin = mix(1.0, perlin, 0.5);
		float worleyLow = CloudWorleyFbm(p, 4, 101u);
		float perlinWorley = CloudRemap01(perlin, 0.0, 1.0, worleyLow, 1.0);
		// eroded by a finer Worley fbm (the shape's G/B/A channels of the four channel versions)
		float worleyHigh = CloudWorleyFbm(p, 8, 211u);
		float shape = CloudRemap01(perlinWorley, worleyHigh - 1.0, 1.0, 0.0, 1.0);
		out_Color = vec4(clamp(shape, 0.0, 1.0), 0.0, 0.0, 1.0);
	}
	else if (target == 1)
	{
		out_Color = vec4(clamp(CloudWorleyFbm(p, 2, 307u), 0.0, 1.0), 0.0, 0.0, 1.0);
	}
	else
	{
		// 2D: a fixed z through the tiling 3D noises
		vec3 q = vec3(p.xy, 0.37);
		float n = CloudPerlinFbm(q, 4, 4, 401u) * 0.5 + 0.5;
		float cells = CloudWorleyFbm(q, 4, 409u);
		float coverage = clamp((mix(n, cells, 0.3) - 0.5) * 2.4 + 0.5, 0.0, 1.0);
		float type = smoothstep(0.35, 0.65, CloudPerlinFbm(q, 2, 2, 503u) * 0.5 + 0.5);
		out_Color = vec4(coverage, type, 0.0, 1.0);
	}
}
