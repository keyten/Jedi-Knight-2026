/*[Compute]*/
// r_fxGpuParticles simulation (tr_fxupgrades.cpp, docs/rend2-fx-upgrades.md).
// One invocation per pool slot. The CPU writes this frame's new particles into
// the ring slots [head, head + count) (no atomics: it owns the ring head).
// Others integrate: gravity / buoyancy, drag, curl noise, then a collision
// with the depth buffer that bounces off the reconstructed surface. The state
// stays in the SSBO; the draw reads the copy written to the image.
layout(local_size_x = 64) in;

struct Particle { vec4 p0; vec4 p1; vec4 p2; vec4 p3; };
// p0: position, remaining life (s); p1: velocity, max life;
// p2: rgb (HDR emission, or dust albedo), type (0 spark, 1 ember, 2 dust);
// p3: size, opacity, bounce, seed
layout(std430, binding = 0) buffer ParticleState { Particle particles[]; };
layout(std430, binding = 1) readonly buffer ParticleSpawn { Particle spawns[]; };
layout(rgba32f, binding = 0) writeonly uniform image2D u_FxOutput;

uniform sampler2D u_ScreenDepthMap;	// copy of the scene depth
uniform mat4 u_FxMatrix[2];			// [0] world to clip, [1] clip to world
uniform vec4 u_FxParams[4];			// [0] head, spawn count, pool size, dt
									// [1] time, curl strength, depth valid, gravity
									// [2] xy unused, z proj[10], w proj[14]
									// [3] xyz view origin

vec3 CurlNoise(vec3 p, float t)
{
	// curl of two octaves of a sine potential: divergence free swirl
	const vec3 f1 = vec3(0.021, 0.017, 0.019);
	const vec3 f2 = vec3(0.047, 0.053, 0.041);
	vec3 c1 = -vec3(f1.y * cos(p.z * f1.y + t * 1.3), f1.z * cos(p.x * f1.z + t * 0.7), f1.x * cos(p.y * f1.x + t));
	vec3 c2 = -vec3(f2.y * cos(p.z * f2.y - t * 2.1), f2.z * cos(p.x * f2.z + t * 1.7), f2.x * cos(p.y * f2.x - t * 1.1));
	return c1 / f1.x + 0.5 * c2 / f2.x;
}

vec3 WorldAt(vec2 uv, float depth)
{
	vec4 w = u_FxMatrix[1] * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
	return w.xyz / w.w;
}

void main()
{
	uint id = gl_GlobalInvocationID.x;
	uint pool = uint(u_FxParams[0].z);
	if (id >= pool)
		return;

	uint head = uint(u_FxParams[0].x);
	uint count = uint(u_FxParams[0].y);
	uint rel = (id + pool - head) % pool;
	Particle part;
	if (rel < count)
		part = spawns[rel];
	else
	{
		part = particles[id];
		if (part.p0.w > 0.0)
		{
			float dt = u_FxParams[0].w;
			float type = part.p2.w;
			vec3 vel = part.p1.xyz;
			// gravity scale, drag, curl response per type
			vec3 typeParams = type < 0.5 ? vec3(1.0, 0.6, 0.3) : (type < 1.5 ? vec3(-0.06, 1.4, 1.0) : vec3(0.02, 2.5, 0.6));
			vel.z -= u_FxParams[1].w * typeParams.x * dt;
			vel += CurlNoise(part.p0.xyz + part.p3.w * 97.0, u_FxParams[1].x) * (u_FxParams[1].y * typeParams.z * dt);
			vel *= exp(-typeParams.y * dt);
			vec3 pos = part.p0.xyz + vel * dt;

			if (u_FxParams[1].z > 0.5)
			{
				vec4 clip = u_FxMatrix[0] * vec4(pos, 1.0);
				if (clip.w > 1.0)
				{
					vec2 ndc = clip.xy / clip.w;
					if (all(lessThan(abs(ndc), vec2(0.995))))
					{
						vec2 uv = ndc * 0.5 + 0.5;
						float depth = textureLod(u_ScreenDepthMap, uv, 0.0).r;
						float sceneDepth = u_FxParams[2].w / (depth * 2.0 - 1.0 + u_FxParams[2].z);
						float thickness = 8.0 + length(vel) * dt * 2.0;
						if (depth < 1.0 && clip.w > sceneDepth && clip.w < sceneDepth + thickness)
						{
							vec2 texel = 1.0 / vec2(textureSize(u_ScreenDepthMap, 0));
							vec3 p = WorldAt(uv, depth);
							vec3 px = WorldAt(uv + vec2(texel.x, 0.0), textureLod(u_ScreenDepthMap, uv + vec2(texel.x, 0.0), 0.0).r);
							vec3 py = WorldAt(uv + vec2(0.0, texel.y), textureLod(u_ScreenDepthMap, uv + vec2(0.0, texel.y), 0.0).r);
							vec3 n = cross(px - p, py - p);
							float nl = length(n);
							n = nl > 1.0e-6 ? n / nl : vec3(0.0, 0.0, 1.0);
							if (dot(n, u_FxParams[3].xyz - p) < 0.0)
								n = -n;
							float vn = dot(vel, n);
							if (vn < 0.0)
							{
								vec3 tangent = vel - n * vn;
								vel = tangent * 0.7 - n * vn * part.p3.z;
								// embers die on contact faster, dust settles
								if (type > 0.5 && type < 1.5)
									part.p0.w *= 0.6;
								if (type > 1.5)
									vel *= 0.2;
							}
							pos = p + n * 0.5;
						}
					}
				}
			}

			part.p0.xyz = pos;
			part.p1.xyz = vel;
			part.p0.w = max(part.p0.w - dt, 0.0);
		}
	}

	particles[id] = part;
	ivec2 texel = ivec2(int(id), 0);
	imageStore(u_FxOutput, texel, part.p0);
	imageStore(u_FxOutput, texel + ivec2(0, 1), part.p1);
	imageStore(u_FxOutput, texel + ivec2(0, 2), part.p2);
	imageStore(u_FxOutput, texel + ivec2(0, 3), part.p3);
}
