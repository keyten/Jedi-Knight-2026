/*[Vertex]*/
// Modern water surface (r_waterSurface, tr_watersurface.cpp, docs/rend2-water-surface.md).
//
// The classified water surfaces of a view are drawn once each, by this program only (not their legacy
// stages), in the water slot of the main pass (RB_SubmitRenderPass): after the opaque surfaces, the
// screen-space passes and the layers up to SS_FOG, before the atmosphere / froxel fog composites and
// the blended layers. The scene under the water was copied there (color + depth, RB_WaterSurfacePrepare).
// The surface keeps the vertex deforms of its shader and the texture coordinate animation (tcMod) of its
// first stage: the flow of the legacy water drives one wave layer.
in vec3 attr_Position;
in vec3 attr_Normal;
in vec2 attr_TexCoord0;

layout(std140) uniform Scene
{
	vec4 u_PrimaryLightOrigin;
	vec3 u_PrimaryLightAmbient;
	int  u_globalFogIndex;
	vec3 u_PrimaryLightColor;
	float u_PrimaryLightRadius;
	float u_frameTime;
	float u_deltaTime;
};

layout(std140) uniform Camera
{
	mat4 u_viewProjectionMatrix;
	vec4 u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
};

layout(std140) uniform Entity
{
	mat4 u_ModelMatrix;
	vec4 u_LocalLightOrigin;
	vec3 u_AmbientLight;
	float u_entityTime;
	vec3 u_DirectedLight;
	float u_FXVolumetricBase;
	vec3 u_ModelLightDir;
	float u_VertexLerp;
};

#if defined(USE_DEFORM_VERTEXES)
layout(std140) uniform ShaderInstance
{
	vec4 u_DeformParams0;
	vec4 u_DeformParams1;
	float u_Time;
	float u_PortalRange;
	int u_DeformType;
	int u_DeformFunc;
};
#endif

uniform vec4 u_DiffuseTexMatrix;
uniform vec4 u_DiffuseTexOffTurb;

out vec3 var_Position;	// world
out vec3 var_Normal;	// world, geometric (out of the liquid for the faces of a liquid brush)
out vec2 var_FlowTex;	// first stage texture coordinates with its tcMods

#if defined(USE_DEFORM_VERTEXES)
float GetNoiseValue( float x, float y, float z, float t )
{
	return fract( sin( dot(
		vec4( x, y, z, t ),
		vec4( 12.9898, 78.233, 12.9898, 78.233 )
	)) * 43758.5453 );
}

float CalculateDeformScale( in int func, in float time, in float phase, in float frequency )
{
	float value = phase + time * frequency;

	switch ( func )
	{
		case WF_SIN:
			return sin(value * 2.0 * M_PI);
		case WF_SQUARE:
			return sign(0.5 - fract(value));
		case WF_TRIANGLE:
			return abs(fract(value + 0.75) - 0.5) * 4.0 - 1.0;
		case WF_SAWTOOTH:
			return fract(value);
		case WF_INVERSE_SAWTOOTH:
			return 1.0 - fract(value);
		default:
			return 0.0;
	}
}

vec3 DeformPosition(const vec3 pos, const vec3 normal, const vec2 st)
{
	switch ( u_DeformType )
	{
		default:
		{
			return pos;
		}

		case DEFORM_BULGE:
		{
			float bulgeHeight = u_DeformParams0.y; // amplitude
			float bulgeWidth = u_DeformParams0.z; // phase
			float bulgeSpeed = u_DeformParams0.w; // frequency

			float scale = CalculateDeformScale( WF_SIN, (u_entityTime + u_frameTime + u_Time), bulgeWidth * st.x, bulgeSpeed );

			return pos + normal * scale * bulgeHeight;
		}

		case DEFORM_BULGE_UNIFORM:
		{
			float bulgeHeight = u_DeformParams0.y; // amplitude

			return pos + normal * bulgeHeight;
		}

		case DEFORM_WAVE:
		{
			float base = u_DeformParams0.x;
			float amplitude = u_DeformParams0.y;
			float phase = u_DeformParams0.z;
			float frequency = u_DeformParams0.w;
			float spread = u_DeformParams1.x;

			float offset = dot( pos.xyz, vec3( spread ) );
			float scale = CalculateDeformScale( u_DeformFunc, (u_entityTime + u_frameTime + u_Time), phase + offset, frequency );

			return pos + normal * (base + scale * amplitude);
		}

		case DEFORM_MOVE:
		{
			float base = u_DeformParams0.x;
			float amplitude = u_DeformParams0.y;
			float phase = u_DeformParams0.z;
			float frequency = u_DeformParams0.w;
			vec3 direction = u_DeformParams1.xyz;

			float scale = CalculateDeformScale( u_DeformFunc, (u_entityTime + u_frameTime + u_Time), phase, frequency );

			return pos + direction * (base + scale * amplitude);
		}
	}
}

vec3 DeformNormal( const in vec3 position, const in vec3 normal )
{
	if ( u_DeformType != DEFORM_NORMALS )
	{
		return normal;
	}

	float amplitude = u_DeformParams0.y;
	float frequency = u_DeformParams0.w;

	vec3 outNormal = normal;
	const float scale = 0.98;

	outNormal.x += amplitude * GetNoiseValue(
		position.x * scale,
		position.y * scale,
		position.z * scale,
		(u_entityTime + u_frameTime + u_Time) * frequency );

	outNormal.y += amplitude * GetNoiseValue(
		100.0 * position.x * scale,
		position.y * scale,
		position.z * scale,
		(u_entityTime + u_frameTime + u_Time) * frequency );

	outNormal.z += amplitude * GetNoiseValue(
		200.0 * position.x * scale,
		position.y * scale,
		position.z * scale,
		(u_entityTime + u_frameTime + u_Time) * frequency );

	return outNormal;
}
#endif

vec2 ModTexCoords(vec2 st, vec3 position, vec4 texMatrix, vec4 offTurb)
{
	float amplitude = offTurb.z;
	float phase = offTurb.w * 2.0 * M_PI;
	vec2 st2;
	st2.x = st.x * texMatrix.x + (st.y * texMatrix.z + offTurb.x);
	st2.y = st.x * texMatrix.y + (st.y * texMatrix.w + offTurb.y);

	vec2 offsetPos = vec2(position.x + position.z, position.y);

	vec2 texOffset = sin(offsetPos * (2.0 * M_PI / 1024.0) + vec2(phase));

	return st2 + texOffset * amplitude;
}

void main()
{
	vec3 position = attr_Position;
	vec3 normal   = attr_Normal * 2.0 - vec3(1.0);

#if defined(USE_DEFORM_VERTEXES)
	position = DeformPosition(position, normal, attr_TexCoord0.st);
	normal = DeformNormal(position, normal);
#endif

	vec4 wsPosition = u_ModelMatrix * vec4(position, 1.0);
	gl_Position = u_viewProjectionMatrix * wsPosition;

	var_Position = wsPosition.xyz;
	var_Normal = normalize(mat3(u_ModelMatrix) * normal);
	var_FlowTex = ModTexCoords(attr_TexCoord0.st, position, u_DiffuseTexMatrix, u_DiffuseTexOffTurb);
}

/*[Fragment]*/
// The surface (dielectric, IOR ~1.333) over the liquid medium, after Unreal's single layer water: the
// opaque scene is complete and lit, the water reads its color and depth and composites
//   (1 - W) * (refracted scene * T + S) + W * reflection + sun / light glints
// W: specular IBL weight of the surface (F0 * EnvBRDF.x + EnvBRDF.y, the exact dielectric Fresnel from
// inside the liquid), T / S: transmittance / in-scattering of the liquid along the refracted path, whose
// length comes from the depth behind the water (a shallow edge is clear, deep water takes the liquid's
// color). Reflection: the screen-space ray march of the SSR inputs (ssr_common.glsl, r_ssr) -> the
// parallax corrected cubemap -> the sky / ambient fallback. Medium: the froxel volume between the
// surface and the scene behind it where it holds this liquid (r_volumetricWater) or fog volume, else
// analytic from the same optical parameters (R_LiquidsMaterial).
//
// Everything is computed in linear light; a legacy (display encoded) HDR scene is decoded and the
// result encoded again (u_Water[5].w).
//
// u_Water: see RB_WaterSurfaceSetupDraw
//   [0] IOR, roughness (perceptual), normal strength, refraction scale
//   [1] reflection scale, SSR scale (0: no SSR), absorption scale, path length scale
//   [2] extinction per unit rgb, froxel volume holds this liquid (0 / 1)
//   [3] single scattering albedo rgb, phase g
//   [4] P[0], P[5], P[8], P[9]
//   [5] P[14], P[10], time (s), linear scene (0 / 1)
//   [6] debug view, split x (window pixels, < 0: none), flags, longest path
//   [7] view right (world), froxel liquid fade start (view depth)
//   [8] view up (world), 1 / froxel liquid fade length
//   [9] fallback environment radiance rgb, sun specular (0 / 1)
//   [10] flow layer scale, 1 / world wave size, unused, Snell debug view (USE_WATER_SNELL)
//   [11] drift of the two world wave layers (texture units, wrapped)
//
// USE_WATER_SNELL (r_waterSnell 1, a permutation: without it the prompt-1 program is unchanged): seen from inside the liquid, the surface is
// the water -> air interface (eta = ior): Snell's window is the refraction of the scene above through it
// and total internal reflection beyond the critical angle comes out of the exact Fresnel (F = 1, nothing
// transmitted), averaged over the unresolved wave slopes; no window mask. The reflected light under the
// surface is the scene in the liquid: SSR -> a cubemap captured in the liquid -> the liquid itself,
// through the medium along the reflected path.

uniform vec4 u_Water[WATER_UNIFORM_VEC4S];

layout(std140) uniform Scene
{
	vec4 u_PrimaryLightOrigin;
	vec3 u_PrimaryLightAmbient;
	int  u_globalFogIndex;
	vec3 u_PrimaryLightColor;
	float u_PrimaryLightRadius;
	float u_frameTime;
	float u_deltaTime;
};

layout(std140) uniform Camera
{
	mat4 u_viewProjectionMatrix;
	vec4 u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
};

struct Light
{
	vec4 origin;
	vec3 color;
	float radius;
	vec4 spot;
	vec4 spot2;
};

layout(std140) uniform Lights
{
	mat4 u_ShadowMvp;
	mat4 u_ShadowMvp2;
	mat4 u_ShadowMvp3;
	vec4 u_ShadowSplits;
	vec4 u_ShadowBlend;
	vec4 u_ShadowTexelSize;
	vec4 u_ShadowDepthSpan;
	vec4 u_ShadowBias;
	vec4 u_ShadowPcss;
	vec4 u_ShadowDebug;
	int u_NumLights;
	Light u_Lights[32];
};

uniform sampler2D u_WaterSceneMap;		// HDR scene under the water (copy)
uniform sampler2D u_WaterDepthMap;		// its hardware depth (copy)
uniform sampler2D u_WaterNormalMap;		// wave slopes (x, y, x^2, y^2), mips keep the variance
uniform sampler2D u_EnvBrdfMap;
uniform samplerCube u_CubeMap;
uniform vec4 u_CubeMapInfo;
#if defined(USE_SHADOWS2)
uniform sampler2DArray u_ShadowMap;
#else
uniform sampler2DArrayShadow u_ShadowMap;
#endif

in vec3 var_Position;
in vec3 var_Normal;
in vec2 var_FlowTex;

out vec4 out_Color;
out vec4 out_Glow;

#ifndef ROUGHNESS_MIPS
#define ROUGHNESS_MIPS 6.0
#endif

#define WATER_FLAG_CUBEMAP	1
#define WATER_FLAG_SUN		2
#define WATER_FLAG_FROXEL	4
#define WATER_FLAG_REJECT	8
#define WATER_FLAG_SSR		16
#define WATER_FLAG_FOGMEDIUM	32
#define WATER_FLAG_SLIME		64
#define WATER_FLAG_OVERRIDE	128
#define WATER_FLAG_EXPERIMENTAL	256
#define WATER_FLAG_ENVBRDF	512
#define WATER_FLAG_CAMERA_LIQUID	1024	// the view origin is in a liquid brush (R_LiquidPointClass)

int g_flags;

bool WaterFlag(int f)
{
	return (g_flags & f) != 0;
}

vec3 SceneToLinear(vec3 c)
{
	if (u_Water[5].w > 0.5)
		return c;
	c = max(c, vec3(0.0));
	vec3 lo = c * (1.0 / 12.92);
	vec3 hi = pow((c + vec3(0.055)) * (1.0 / 1.055), vec3(2.4));
	return mix(lo, hi, greaterThan(c, vec3(0.04045)));
}

vec3 LinearToScene(vec3 c)
{
	if (u_Water[5].w > 0.5)
		return c;
	c = max(c, vec3(0.0));
	vec3 lo = 12.92 * c;
	vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
	return mix(lo, hi, greaterThanEqual(c, vec3(0.0031308)));
}

// linear view depth of a hardware depth value
float WaterLinearDepth(float d)
{
	return u_Water[5].x / (d * 2.0 - 1.0 + u_Water[5].y);
}

bool WaterIsSky(float d)
{
	return d >= 0.999999;
}

// world position of render target uv at view depth z
vec3 WaterWorldPosition(vec2 uv, float z)
{
	vec2 ndc = uv * 2.0 - 1.0;
	vec2 xy = (ndc + u_Water[4].zw) * z / u_Water[4].xy;
	return u_ViewOrigin + u_Water[7].xyz * xy.x + u_Water[8].xyz * xy.y + normalize(u_ViewForward) * z;
}

vec2 WaterProject(vec3 p)
{
	vec4 clip = u_viewProjectionMatrix * vec4(p, 1.0);
	return clip.xy / max(clip.w, 1e-4) * 0.5 + 0.5;
}

// render target uv of the point L along the refracted ray Rt from P, the offset scaled by
// r_waterSurfaceRefraction and bounded
vec2 WaterRefractedUV(vec3 P, vec3 Rt, float L, vec2 uv)
{
	vec2 offset = (WaterProject(P + Rt * L) - uv) * u_Water[0].w;
	float offsetLength = length(offset);
	if (offsetLength > 0.12)
		offset *= 0.12 / offsetLength;
	return clamp(uv + offset, vec2(0.0), vec2(1.0));
}

// depth below the surface plane (P, Ng) of the scene sample at uv (depth: its hardware depth), the
// position taken at the center of the depth texel the sample came from
float WaterDepthBelow(vec3 P, vec3 Ng, vec2 uv, float depth)
{
	vec2 texel = (floor(uv * r_FBufScale) + 0.5) / r_FBufScale;
	return dot(P - WaterWorldPosition(texel, WaterLinearDepth(depth)), Ng);
}

// unpolarized Fresnel reflectance of a smooth dielectric, eta = n(incident side) / n(other side)
float FresnelDielectric(float cosi, float eta)
{
	float sint2 = eta * eta * max(1.0 - cosi * cosi, 0.0);
	if (sint2 >= 1.0)
		return 1.0;
	float cost = sqrt(1.0 - sint2);
	float rs = (eta * cosi - cost) / (eta * cosi + cost);
	float rp = (cosi - eta * cost) / (cosi + eta * cost);
	return 0.5 * (rs * rs + rp * rp);
}

// beyond the critical angle (eta > 1 only): total internal reflection
float WaterTIR(float cosi, float eta)
{
	return eta * eta * max(1.0 - cosi * cosi, 0.0) >= 1.0 ? 1.0 : 0.0;
}

float D_GGX(float NH, float a)
{
	float a2 = a * a;
	float d = (NH * a2 - NH) * NH + 1.0;
	return a2 / (M_PI * d * d);
}

float V_SmithJointApprox(float a, float NV, float NL)
{
	float Vis_SmithV = NL * (NV * (1.0 - a) + a);
	float Vis_SmithL = NV * (NL * (1.0 - a) + a);
	return 0.5 / (Vis_SmithV + Vis_SmithL);
}

// GGX glint of a light of color c from direction L (alpha = GGX alpha)
vec3 WaterGlint(vec3 N, vec3 V, vec3 L, vec3 c, float alpha, float ior)
{
	float NL = dot(N, L);
	if (NL <= 0.0)
		return vec3(0.0);
	vec3 H = normalize(L + V);
	float NH = clamp(dot(N, H), 0.0, 1.0);
	float VH = clamp(dot(V, H), 0.0, 1.0);
	float NV = max(dot(N, V), 1e-4);
	float F = FresnelDielectric(VH, 1.0 / ior);
	return c * (D_GGX(NH, alpha) * V_SmithJointApprox(alpha, NV, NL) * F * NL);
}

// Henyey-Greenstein, normalized over the sphere (1 / 4 pi when isotropic)
float WaterPhase(float g, float cosTheta)
{
	float g2 = g * g;
	return (1.0 - g2) / (4.0 * M_PI * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5));
}

// The homogeneous liquid along a ray leaving the surface downwards in direction dir, d units long:
// returns the in-scattered radiance towards the surface, T its transmittance. Half the ambient from
// above, and the sun (sunLight = pi * color * shadow, 0 without) refracted into the liquid
// (sunInLiquid, cosine sunCos), attenuated down to the depth of each point:
//   sun: integral of sigma albedo L p exp(-sigma t (1 + k)), k = depth gained per unit / sunCos
vec3 WaterLiquidPath(vec3 dir, float d, vec3 Ng, vec3 sigma, vec3 albedo, float g, vec3 ambientUp,
	vec3 sunLight, vec3 sunInLiquid, float sunCos, out vec3 T)
{
	T = exp(-sigma * d);
	vec3 S = albedo * (0.5 * ambientUp) * (vec3(1.0) - T);
	float k = max(-dot(dir, Ng), 0.0) / sunCos;
	S += albedo * sunLight * (WaterPhase(g, dot(dir, -sunInLiquid)) / (1.0 + k)) *
		(vec3(1.0) - exp(-sigma * ((1.0 + k) * d)));
	return S;
}

float WaterShadowTap(vec3 s, float layer)
{
#if defined(USE_SHADOWS2)
	return step(s.z - 0.0005, texture(u_ShadowMap, vec3(s.xy, layer)).r);
#else
	return texture(u_ShadowMap, vec4(s.xy, layer, s.z - 0.0005));
#endif
}

float WaterShadowCascade(vec3 s, float layer, vec2 texel)
{
	return 0.25 * (
		WaterShadowTap(s + vec3(-texel.x, -texel.y, 0.0), layer) +
		WaterShadowTap(s + vec3( texel.x, -texel.y, 0.0), layer) +
		WaterShadowTap(s + vec3(-texel.x,  texel.y, 0.0), layer) +
		WaterShadowTap(s + vec3( texel.x,  texel.y, 0.0), layer));
}

bool WaterInCascade(vec3 s, float margin)
{
	return all(lessThanEqual(abs(s - vec3(0.5)), vec3(0.5 - margin)));
}

vec3 WaterShadowProject(mat4 m, vec3 p)
{
	vec4 q = m * vec4(p, 1.0);
	return q.xyz / q.w * 0.5 + 0.5;
}

// sun visibility of a point (the sun cascades of the view; water casts no shadow)
float WaterSunShadow(vec3 p)
{
	vec2 texel = 1.5 / vec2(textureSize(u_ShadowMap, 0).xy);
	float margin = 2.0 * texel.x;
	vec3 s = WaterShadowProject(u_ShadowMvp, p);
	if (WaterInCascade(s, margin))
		return WaterShadowCascade(s, 0.0, texel);
	s = WaterShadowProject(u_ShadowMvp2, p);
	if (WaterInCascade(s, margin))
		return WaterShadowCascade(s, 1.0, texel);
	s = WaterShadowProject(u_ShadowMvp3, p);
	if (WaterInCascade(s, margin))
		return WaterShadowCascade(s, 2.0, texel);
	return 1.0;
}

float WaterLightAttenuation(float normDist)
{
	return clamp(0.5 * normDist - 0.5, 0.0, 1.0);
}

// radiance of the environment in direction R (cubemap or the fallback), linear
vec3 WaterEnvironment(vec3 P, vec3 R, float lod)
{
	if (WaterFlag(WATER_FLAG_CUBEMAP))
	{
		// parallax corrected as lightall (CalcIBLRadiance)
		vec3 parallax = u_CubeMapInfo.xyz + u_CubeMapInfo.w * (u_ViewOrigin - P);
		return SceneToLinear(textureLod(u_CubeMap, R - parallax, lod).rgb);
	}
	// no cubemap: the ambient of the sky, brighter towards the zenith
	return SceneToLinear(u_Water[9].rgb) * mix(0.55, 1.0, clamp(R.z * 0.5 + 0.5, 0.0, 1.0));
}

#if defined(USE_SSR)
// Screen-space reflection of the opaque scene along R (view space ray march of ssr_common.glsl,
// linear steps). rgb = radiance (linear), a = confidence.
//   u_SSRSettings  steps, refine steps, max distance, thickness
//   u_SSRSettings2 max roughness, edge fade, coarsest color mip
//   u_SSRSettings3 coarsest Hi-Z level, unused, near plane, Hi-Z iterations (USE_HIZ)
// Ngeo: the geometric normal on the side the ray leaves from; hitDistance: world units to the hit
vec4 WaterSSRTrace(vec3 Pw, vec3 Rw, vec3 Ngeo, float roughness, out float hitDistance)
{
	hitDistance = 0.0;
	vec3 right = u_Water[7].xyz;
	vec3 up = u_Water[8].xyz;
	vec3 forward = normalize(u_ViewForward);
	vec3 rel = Pw - u_ViewOrigin;
	vec3 P = vec3(dot(rel, right), dot(rel, up), dot(rel, forward));
	vec3 R = normalize(vec3(dot(Rw, right), dot(Rw, up), dot(Rw, forward)));
	vec3 N = vec3(dot(Ngeo, right), dot(Ngeo, up), dot(Ngeo, forward));
	vec3 V = -normalize(P);

	float startOffset = max(0.5, 0.002 * P.z);
	vec3 O = P + N * startOffset;
	float rayLength = u_SSRSettings.z;
	float nearZ = u_SSRSettings3.z * 1.5;
	if (R.z < 0.0)
		rayLength = min(rayLength, (O.z - nearZ) / -R.z);
	if (rayLength <= 1.0 || O.z <= nearZ)
		return vec4(0.0);

	vec3 E = O + R * rayLength;
	float sMin, sMax, screenLength;
	if (!SSRSetupRay(O, E, sMin, sMax, screenLength))
		return vec4(0.0);

	float jitter = SSRInterleavedGradientNoise(gl_FragCoord.xy);
	float sHit;
	if (!SSRMarchRay(sMin, sMax, screenLength, jitter, u_SSRSettings.x, 1.0, u_SSRSettings.w,
		u_SSRSettings.y, u_SSRSettings3.x, u_SSRSettings3.w, sHit))
		return vec4(0.0);

	vec2 hitPixel = SSRRayPixel(sHit);
	float zRay = SSRRayDepth(sHit);
	float zScene = SSRSceneDepth(hitPixel);
	if (!SSRIsSurface(zScene))
		return vec4(0.0);
	vec3 Q = mix(O * g_k0, E * g_k1, sHit) * zRay;
	vec2 hitUV = hitPixel * u_SSRTexelSize.xy;
	hitDistance = length(Q - O);
	if (hitDistance < 2.0 * startOffset)
		return vec4(0.0);

	// the confidence terms of the SSR trace (ssr_trace.glsl), the grazing fade on the flat surface
	float confidence = 1.0 - smoothstep(0.25, 1.0, abs(zRay - zScene) / SSRThickness(zScene, u_SSRSettings.w));
	if (u_SSRSettings2.y > 0.0)
	{
		vec2 relUV = (hitUV - u_SSRViewport.xy) / u_SSRViewport.zw;
		float edge = min(min(relUV.x, 1.0 - relUV.x), min(relUV.y, 1.0 - relUV.y));
		confidence *= smoothstep(0.0, u_SSRSettings2.y, edge);
	}
	confidence *= 1.0 - smoothstep(0.6, 1.0, hitDistance / rayLength);
	confidence *= 1.0 - smoothstep(0.4, 0.9, dot(R, V));
	confidence *= smoothstep(0.0, 0.02, dot(N, V));
	confidence *= 1.0 - smoothstep(u_SSRSettings2.x * 0.7, u_SSRSettings2.x, roughness);
	if (confidence <= 0.0)
		return vec4(0.0);

	// radiance: the color pyramid at the cone footprint (ssr_resolve.glsl HitRadiance)
	float footprint = 2.0 * hitDistance * SSRConeTangent(roughness) / (max(Q.z, 1.0) * u_SSRDepthParams.w);
	float mip = clamp(log2(max(footprint, 1.0)), 0.0, u_SSRSettings2.z);
	vec4 c = textureLod(u_SSRSceneMap, hitUV, max(mip, 1.0));
	if (mip < 1.0)
		c = mix(vec4(textureLod(u_SSRSceneMap, hitUV, 0.0).rgb, 1.0), c, mip);
	confidence *= smoothstep(0.2, 0.6, c.a);
	return vec4(SceneToLinear(c.rgb / max(c.a, 1.0e-3)), confidence);
}

vec4 WaterSSR(vec3 Pw, vec3 Rw, vec3 Ngeo, float roughness)
{
	float hitDistance;
	return WaterSSRTrace(Pw, Rw, Ngeo, roughness, hitDistance);
}
#endif

// medium between the surface Ps and the scene point behind it along the view ray (froxel volume),
// linear in-scattering and transmittance
void WaterFroxelSegment(vec3 Ps, vec3 Pb, out vec3 S, out vec3 T)
{
	S = vec3(0.0);
	T = vec3(1.0);
#if defined(USE_FROXEL_FOG)
	if (!WaterFlag(WATER_FLAG_FROXEL))
		return;
  #if defined(USE_FROXEL_RGB)
	vec3 T0, T1;
	vec3 S0 = FroxelFogRGB(Ps, T0);
	vec3 S1 = FroxelFogRGB(Pb, T1);
	vec3 inv = 1.0 / max(T0, vec3(1e-4));
	T = clamp(T1 * inv, 0.0, 1.0);
	S = max(S1 - S0, vec3(0.0)) * inv;
  #else
	vec4 f0 = FroxelFog(Ps);
	vec4 f1 = FroxelFog(Pb);
	float inv = 1.0 / max(f0.a, 1e-4);
	T = vec3(clamp(f1.a * inv, 0.0, 1.0));
	S = max(f1.rgb - f0.rgb, vec3(0.0)) * inv;
  #endif
	S = SceneToLinear(S);
#endif
}

void main()
{
	g_flags = int(u_Water[6].z + 0.5);
	int debugView = int(u_Water[6].x + 0.5);

	// split view: the legacy stages are drawn on the left (scissor), the water program on the right
	if (u_Water[6].y >= 0.0 && gl_FragCoord.x < u_Water[6].y)
		discard;

	vec2 uv = gl_FragCoord.xy / r_FBufScale;
	vec4 sceneHere = texture(u_WaterSceneMap, uv);

	float ior = u_Water[0].x;
	vec3 P = var_Position;
	vec3 toCamera = u_ViewOrigin - P;
	float cameraDistance = max(length(toCamera), 1e-3);
	vec3 V = toCamera / cameraDistance;
	vec3 Ng = normalize(var_Normal);
	// the normals of a liquid brush point out of the liquid: the camera behind the face is inside it
	bool inside = dot(V, Ng) < 0.0;

	// waves: tiling slope texture, two world space layers drifting with the wind and one layer on the
	// first stage coordinates (its tcMod scroll is the flow of the legacy water). The mips keep the
	// slope variance: distant waves become roughness instead of aliasing (LEAN).
	vec3 Tw = abs(Ng.z) > 0.7 ? vec3(1.0, 0.0, 0.0) : normalize(cross(vec3(0.0, 0.0, 1.0), Ng));
	vec3 Bw = normalize(cross(Ng, Tw));
	vec2 planar = vec2(dot(P, Tw), dot(P, Bw));
	float invSize = u_Water[10].y;
	vec4 l0 = texture(u_WaterNormalMap, planar * invSize + u_Water[11].xy);
	vec4 l1 = texture(u_WaterNormalMap, planar * (invSize * 2.37) + u_Water[11].zw);
	vec4 l2 = texture(u_WaterNormalMap, var_FlowTex * u_Water[10].x);
	float strength = u_Water[0].z * 0.18;
	vec2 slope = (l0.xy + 0.6 * l1.xy + 0.5 * l2.xy) * strength;
	vec2 variance = (max(l0.zw - l0.xy * l0.xy, vec2(0.0)) +
		0.36 * max(l1.zw - l1.xy * l1.xy, vec2(0.0)) +
		0.25 * max(l2.zw - l2.xy * l2.xy, vec2(0.0))) * (strength * strength);
	vec3 Nwater = normalize(Ng - slope.x * Tw - slope.y * Bw);
	vec3 N = inside ? -Nwater : Nwater;
	vec3 Nside = inside ? -Ng : Ng;	// geometric normal on the camera side

	// roughness: the artistic base plus the unresolved wave slopes (GGX alpha^2 ~ 2 variance)
	float baseRoughness = u_Water[0].y;
	float alpha = sqrt(min(baseRoughness * baseRoughness * baseRoughness * baseRoughness +
		(variance.x + variance.y), 1.0));
	alpha = max(alpha, 0.002);
	float roughness = sqrt(alpha);

	float NV = max(dot(N, V), 1e-4);
	float F = FresnelDielectric(NV, inside ? ior : 1.0 / ior);
	float W = F;
	if (!inside && WaterFlag(WATER_FLAG_ENVBRDF))
	{
		float F0 = (ior - 1.0) / (ior + 1.0);
		F0 *= F0;
		vec2 envBrdf = texture(u_EnvBrdfMap, vec2(roughness, NV)).rg;
		W = clamp(F0 * envBrdf.x + envBrdf.y, 0.0, 1.0);
	}

	// Snell's window (r_waterSnell): from inside, the exact water -> air Fresnel averaged over the
	// unresolved wave slopes (the centre and +- one deviation of each slope), so the critical angle
	// broadens with the waves of a pixel instead of aliasing; TIR in all of them: W = 1 exactly. The
	// refracted direction follows the centre normal, or the most transmissive one when it reflects totally.
#if defined(USE_WATER_SNELL)
	int snellDebug = int(u_Water[10].w + 0.5);
	float tirFraction = 0.0;
	vec3 Nrefract = N;
	if (inside)
	{
		vec2 deviation = sqrt(variance);
		float Fsum = F;
		float Fmin = 2.0;
		vec3 Nmin = N;
		tirFraction = WaterTIR(NV, ior);
		for (int k = 0; k < 4; k++)
		{
			vec2 o = k == 0 ? vec2(deviation.x, 0.0) : k == 1 ? vec2(-deviation.x, 0.0) :
				k == 2 ? vec2(0.0, deviation.y) : vec2(0.0, -deviation.y);
			vec3 Nk = -normalize(Ng - (slope.x + o.x) * Tw - (slope.y + o.y) * Bw);
			float cosk = max(dot(Nk, V), 1e-4);
			float Fk = FresnelDielectric(cosk, ior);
			Fsum += Fk;
			tirFraction += WaterTIR(cosk, ior);
			if (Fk < Fmin)
			{
				Fmin = Fk;
				Nmin = Nk;
			}
		}
		W = Fsum * 0.2;
		tirFraction *= 0.2;
		if (WaterTIR(NV, ior) > 0.5)
			Nrefract = Nmin;
	}
#endif

	// view depths: the surface and the scene behind it
	float zSurface = 1.0 / gl_FragCoord.w;
	float depthHere = texture(u_WaterDepthMap, uv).r;
	bool skyHere = WaterIsSky(depthHere);
	float zHere = skyHere ? 1.0e6 : max(WaterLinearDepth(depthHere), zSurface);
	float maxPath = u_Water[6].w;

	// liquid optics
	vec3 sigma = u_Water[2].rgb * u_Water[1].z;
	vec3 albedo = u_Water[3].rgb;
	float g = u_Water[3].a;
	vec3 sunDir = normalize(u_PrimaryLightOrigin.xyz);
	vec3 sunColor = SceneToLinear(u_PrimaryLightColor);
	float sunShadow = 1.0;
	bool sun = u_Water[9].w > 0.5;
	if (sun)
		sunShadow = WaterSunShadow(P);
	vec3 ambient = WaterEnvironment(P, Nside, ROUGHNESS_MIPS);

	// light inside the liquid (single scattering, homogeneous medium): half of the ambient from above,
	// the sun along the refracted path down to depth h
	vec3 viewInLiquid = inside ? -V : refract(-V, Nwater, 1.0 / ior);
	vec3 sunInLiquid = refract(-sunDir, Ng, 1.0 / ior);
	float sunCos = max(-dot(sunInLiquid, Ng), 0.1);

	vec3 reflection = vec3(0.0);
	vec3 transmitted = vec3(0.0);
	vec3 glint = vec3(0.0);
	vec3 debugSource = vec3(0.0);
	vec4 ssrDebug = vec4(0.0);
	vec3 rawRefracted = vec3(0.0);
	vec3 transmittance = vec3(1.0);
	float pathLength = 0.0;
	float rejected = 0.0;
#if defined(USE_WATER_SNELL)
	vec3 snellRt = vec3(0.0);
#endif

	if (!inside)
	{
		// ---- reflection: SSR -> cubemap -> fallback
		vec3 R = reflect(-V, N);
		float below = dot(R, Ng);
		if (below < 0.02)
			R = normalize(R + Ng * (0.02 - below));
		vec3 env = WaterEnvironment(P, R, roughness * ROUGHNESS_MIPS);
		debugSource = WaterFlag(WATER_FLAG_CUBEMAP) ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
#if defined(USE_SSR)
		if (WaterFlag(WATER_FLAG_SSR) && u_Water[1].y > 0.0)
		{
			vec4 ssr = WaterSSR(P, R, Ng, roughness);
			float c = ssr.a * u_Water[1].y;
			env = mix(env, ssr.rgb, c);
			debugSource = mix(debugSource, vec3(1.0, 0.0, 0.0), c);
			ssrDebug = vec4(ssr.a > 0.0 ? vec3(0.0, ssr.a, 0.0) : vec3(0.35, 0.0, 0.0), 1.0);
		}
#endif
		reflection = env * u_Water[1].x;

		// ---- refraction: the refracted ray down to the scene behind the water. The first guess of its
		// length is the depth below the surface of the scene straight behind the pixel (bounded: behind
		// a far shore or the sky it is unknown and would push the sample to the horizon); two fixed point
		// steps take the depth found at the sample.
		float pathStraight = skyHere ? maxPath : (zHere - zSurface) * cameraDistance / zSurface;
		vec3 Rt = refract(-V, N, 1.0 / ior);
		float cosT = max(-dot(Rt, Ng), 0.05);
		float depthGuess = skyHere ? 256.0 : min(pathStraight * max(dot(V, Ng), 0.0), 2048.0);
		vec2 uvR = WaterRefractedUV(P, Rt, depthGuess / cosT, uv);
		float depthR = texture(u_WaterDepthMap, uvR).r;
		for (int i = 0; i < 2; i++)
		{
			if (WaterIsSky(depthR))
				break;
			float h = WaterDepthBelow(P, Ng, uvR, depthR);
			if (h <= 0.0)
				break;
			uvR = WaterRefractedUV(P, Rt, h / cosT, uv);
			depthR = texture(u_WaterDepthMap, uvR).r;
		}
		vec2 offset = uvR - uv;

		// a foreground object (in front of the water at that pixel) is not under the water: halve the
		// offset until the sample is behind the surface, else no refraction
		if (WaterFlag(WATER_FLAG_REJECT))
		{
			float k = 1.0;
			for (int i = 0; i < 3; i++)
			{
				if (WaterIsSky(depthR) || WaterLinearDepth(depthR) > zSurface + 0.5)
					break;
				k *= 0.5;
				uvR = clamp(uv + offset * k, vec2(0.0), vec2(1.0));
				depthR = texture(u_WaterDepthMap, uvR).r;
			}
			if (!WaterIsSky(depthR) && WaterLinearDepth(depthR) <= zSurface + 0.5)
			{
				uvR = uv;
				depthR = depthHere;
				rejected = 1.0;
			}
			else if (k < 1.0)
				rejected = 0.5;
		}

		// path inside the liquid: the depth of the refracted scene point below the surface plane
		if (WaterIsSky(depthR))
			pathLength = maxPath;
		else
			pathLength = clamp(max(WaterDepthBelow(P, Ng, uvR, depthR), 0.0) / cosT, 0.0, maxPath);
		pathLength *= u_Water[1].w;

		rawRefracted = SceneToLinear(texture(u_WaterSceneMap, uvR).rgb);

		// medium: the froxel volume where it holds the liquid (fading out with the volume), the analytic
		// medium of the same parameters elsewhere
		vec3 S = vec3(0.0);
		vec3 T = vec3(1.0);
		float analytic = 1.0;
		if (u_Water[2].w > 0.5)
			analytic = clamp((zSurface - u_Water[7].w) * u_Water[8].w, 0.0, 1.0);
		if (WaterFlag(WATER_FLAG_FOGMEDIUM))
			analytic = 0.0;
		vec3 Pstraight = skyHere ? P - V * maxPath : u_ViewOrigin + (P - u_ViewOrigin) * (zHere / zSurface);
		WaterFroxelSegment(P, Pstraight, S, T);

		vec3 Ta = exp(-sigma * (pathLength * analytic));
		vec3 sunIn = vec3(0.0);
		if (sun)
		{
			vec3 sunT = exp(-sigma * (0.5 * pathLength * max(dot(-Rt, Ng), 0.05) / sunCos));
			sunIn = M_PI * sunColor * sunShadow * WaterPhase(g, dot(Rt, -sunInLiquid)) * sunT;
		}
		vec3 Sa = albedo * (0.5 * ambient + sunIn) * (vec3(1.0) - Ta);
		transmittance = T * Ta;
		transmitted = rawRefracted * transmittance + S + T * Sa;

		// ---- glints: the sun and the dynamic lights (sabers, bolts, explosions)
		float glintAlpha = max(alpha, 0.01);
		if (sun)
			glint += WaterGlint(N, V, sunDir, sunColor * sunShadow, glintAlpha, ior);
		for (int i = 0; i < u_NumLights; i++)
		{
			vec3 L = u_Lights[i].origin.xyz - P;
			float r = u_Lights[i].radius;
			float d2 = max(dot(L, L), 1.0);
			float attenuation = WaterLightAttenuation(r * r / d2);
			if (attenuation <= 0.0)
				continue;
			L *= inversesqrt(d2);
			vec4 spot = u_Lights[i].spot;
			attenuation *= smoothstep(spot.w, u_Lights[i].spot2.x, dot(-L, spot.xyz));
			glint += WaterGlint(N, V, L, SceneToLinear(u_Lights[i].color) * attenuation, max(alpha, 0.03), ior);
		}
	}
	else if (!skyHere && (zHere - zSurface) * cameraDistance / zSurface < 4.0)
	{
		// seen from inside the liquid with the scene right behind the face: a face of the liquid brush
		// against the floor or a wall, not an interface with air (no false total internal reflection)
		transmitted = SceneToLinear(sceneHere.rgb);
		rawRefracted = transmitted;
		W = 0.0;
	}
#if defined(USE_WATER_SNELL)
	else
	{
		// ---- seen from inside the liquid, r_waterSnell: the water -> air interface (eta = ior)
		// Snell's window: the refracted ray up to the scene above, as from above (bounded first guess from
		// the scene straight behind, two fixed point steps on its height above the surface plane). The air
		// above holds no medium here.
		vec3 Rt = refract(-V, Nrefract, ior);
		snellRt = Rt;
		if (dot(Rt, Rt) > 0.0)
		{
			float cosT = max(dot(Rt, Ng), 0.05);
			float pathStraight = skyHere ? maxPath : (zHere - zSurface) * cameraDistance / zSurface;
			float heightGuess = skyHere ? 256.0 : min(pathStraight * max(-dot(V, Ng), 0.0), 2048.0);
			vec2 uvR = WaterRefractedUV(P, Rt, heightGuess / cosT, uv);
			float depthR = texture(u_WaterDepthMap, uvR).r;
			for (int i = 0; i < 2; i++)
			{
				if (WaterIsSky(depthR))
					break;
				float h = -WaterDepthBelow(P, Ng, uvR, depthR);
				if (h <= 0.0)
					break;
				uvR = WaterRefractedUV(P, Rt, h / cosT, uv);
				depthR = texture(u_WaterDepthMap, uvR).r;
			}

			// a sample in front of the surface is in the liquid between the camera and the surface
			if (WaterFlag(WATER_FLAG_REJECT))
			{
				vec2 offset = uvR - uv;
				float k = 1.0;
				for (int i = 0; i < 3; i++)
				{
					if (WaterIsSky(depthR) || WaterLinearDepth(depthR) > zSurface + 0.5)
						break;
					k *= 0.5;
					uvR = clamp(uv + offset * k, vec2(0.0), vec2(1.0));
					depthR = texture(u_WaterDepthMap, uvR).r;
				}
				if (!WaterIsSky(depthR) && WaterLinearDepth(depthR) <= zSurface + 0.5)
				{
					uvR = uv;
					rejected = 1.0;
				}
				else if (k < 1.0)
					rejected = 0.5;
			}
			rawRefracted = SceneToLinear(texture(u_WaterSceneMap, uvR).rgb);
			transmitted = rawRefracted;
		}

		// ---- reflection under the surface (total beyond the critical angle): the scene in the liquid,
		// SSR -> a cubemap captured in the liquid -> the liquid itself (the in-scattering of an endless
		// path), each through the medium along the reflected path. No reflection scale: W is the physical
		// reflectance (1 in total internal reflection).
		vec3 R = reflect(-V, N);
		float into = dot(R, Ng);
		if (into > -0.02)
			R = normalize(R - Ng * (into + 0.02));
		vec3 ambientUp = WaterEnvironment(P, Ng, ROUGHNESS_MIPS);
		vec3 sunLight = sun ? M_PI * sunColor * sunShadow : vec3(0.0);
		float pathScale = u_Water[1].w;
		vec3 Tr;
		reflection = WaterLiquidPath(R, maxPath, Ng, sigma, albedo, g, ambientUp, sunLight, sunInLiquid, sunCos, Tr);
		debugSource = vec3(0.0, 0.0, 1.0);
		if (WaterFlag(WATER_FLAG_CUBEMAP) && u_CubeMapInfo.w > 0.0)
		{
			// a probe above the surface captured the air side: only a probe in the liquid
			vec3 probe = u_ViewOrigin + u_CubeMapInfo.xyz / u_CubeMapInfo.w;
			if (dot(probe - P, Ng) < 0.0)
			{
				float d = min(1.0 / u_CubeMapInfo.w, maxPath) * pathScale;
				vec3 S = WaterLiquidPath(R, d, Ng, sigma, albedo, g, ambientUp, sunLight, sunInLiquid, sunCos, Tr);
				reflection = WaterEnvironment(P, R, roughness * ROUGHNESS_MIPS) * Tr + S;
				debugSource = vec3(0.0, 1.0, 0.0);
			}
		}
#if defined(USE_SSR)
		if (WaterFlag(WATER_FLAG_SSR) && u_Water[1].y > 0.0)
		{
			float hitDistance;
			vec4 ssr = WaterSSRTrace(P, R, Nside, roughness, hitDistance);
			float c = ssr.a * u_Water[1].y;
			if (c > 0.0)
			{
				vec3 S = WaterLiquidPath(R, hitDistance * pathScale, Ng, sigma, albedo, g, ambientUp, sunLight,
					sunInLiquid, sunCos, Tr);
				reflection = mix(reflection, ssr.rgb * Tr + S, c);
				debugSource = mix(debugSource, vec3(1.0, 0.0, 0.0), c);
			}
			ssrDebug = vec4(ssr.a > 0.0 ? vec3(0.0, ssr.a, 0.0) : vec3(0.35, 0.0, 0.0), 1.0);
		}
#endif
	}
#else
	else
	{
		// seen from inside the liquid: Snell's window (the scene above, refracted) and total internal
		// reflection of the liquid itself
		vec3 deep = albedo * (0.5 * ambient + (sun ? M_PI * sunColor * sunShadow * WaterPhase(g, dot(-V, -sunInLiquid)) : vec3(0.0)));
		reflection = deep;
		vec3 Rt = refract(-V, N, ior);
		if (dot(Rt, Rt) > 0.0)
		{
			vec2 offset = (WaterProject(P + Rt * 256.0) - uv) * u_Water[0].w;
			float offsetLength = length(offset);
			if (offsetLength > 0.12)
				offset *= 0.12 / offsetLength;
			vec2 uvR = clamp(uv + offset, vec2(0.0), vec2(1.0));
			if (WaterFlag(WATER_FLAG_REJECT))
			{
				float depthR = texture(u_WaterDepthMap, uvR).r;
				if (!WaterIsSky(depthR) && WaterLinearDepth(depthR) <= zSurface + 0.5)
				{
					uvR = uv;
					rejected = 1.0;
				}
			}
			rawRefracted = SceneToLinear(texture(u_WaterSceneMap, uvR).rgb);
			transmitted = rawRefracted;
		}
		debugSource = vec3(0.0, 0.0, 1.0);
	}
#endif

	vec3 color = (1.0 - W) * transmitted + W * reflection + glint;

#if defined(USE_WATER_SNELL)
	if (snellDebug != 0)
	{
		vec3 d = vec3(0.0);
		if (snellDebug == 1)
		{
			// direction of the crossing: blue air -> water (eta 1 / ior), orange water -> air (eta ior);
			// magenta stripes: the camera contents disagree with the side of this fragment
			d = inside ? vec3(1.0, 0.5, 0.1) : vec3(0.1, 0.45, 1.0);
			bool cameraLiquid = WaterFlag(WATER_FLAG_CAMERA_LIQUID);
			if (cameraLiquid != inside && mod(gl_FragCoord.x + gl_FragCoord.y, 16.0) < 6.0)
				d = vec3(1.0, 0.0, 1.0);
		}
		else if (snellDebug == 2)
		{
			// red: total internal reflection (share of the wave slopes), green: transmits; yellow line:
			// the critical angle of the centre normal; dark blue: from air (no total internal reflection)
			if (inside)
			{
				d = vec3(tirFraction, 1.0 - tirFraction, 0.0);
				float sinI = sqrt(max(1.0 - NV * NV, 0.0));
				if (abs(sinI * ior - 1.0) < 0.012)
					d = vec3(1.0, 1.0, 0.0);
			}
			else
				d = vec3(0.0, 0.0, 0.3);
		}
		else if (snellDebug == 3)
			d = vec3(FresnelDielectric(NV, inside ? ior : 1.0 / ior));
		else if (snellDebug == 4)
		{
			// refracted direction (world, * 0.5 + 0.5); black: total internal reflection
			vec3 Rt = inside ? snellRt : refract(-V, N, 1.0 / ior);
			d = dot(Rt, Rt) > 0.0 ? Rt * 0.5 + 0.5 : vec3(0.0);
		}
		else if (snellDebug == 5)
			d = vec3(W, 1.0 - W, 0.0);	// red: reflection weight, green: transmission weight
		else if (snellDebug == 6)
			d = debugSource;	// red SSR, green cubemap, blue liquid (from inside) / fallback (from air)
		out_Color = vec4(LinearToScene(d), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}
#endif

	if (debugView != 0)
	{
		vec3 d = vec3(0.0);
		if (debugView == 1)
		{
			// blue water, green slime optics, violet fog volume medium; yellow / magenta: decided by
			// r_waterOverride / the experimental name rule; darker: seen from inside the liquid
			d = WaterFlag(WATER_FLAG_SLIME) ? vec3(0.3, 1.0, 0.15) : vec3(0.1, 0.45, 1.0);
			if (WaterFlag(WATER_FLAG_FOGMEDIUM))
				d = vec3(0.6, 0.2, 1.0);
			if (WaterFlag(WATER_FLAG_OVERRIDE))
				d = vec3(1.0, 0.85, 0.1);
			if (WaterFlag(WATER_FLAG_EXPERIMENTAL))
				d = vec3(1.0, 0.2, 0.8);
			d *= inside ? 0.4 : 1.0;
		}
		else if (debugView == 2)
			d = N * 0.5 + 0.5;
		else if (debugView == 3)
			d = vec3(W);
		else if (debugView == 4)
			d = vec3(clamp(pathLength / 512.0, 0.0, 1.0), clamp(pathLength / 128.0, 0.0, 1.0), clamp(pathLength / 32.0, 0.0, 1.0));
		else if (debugView == 5)
			d = rawRefracted;
		else if (debugView == 6)
			d = ssrDebug.a > 0.0 ? ssrDebug.rgb : vec3(0.0);
		else if (debugView == 7)
			d = debugSource;
		else if (debugView == 8)
			d = transmittance;
		else if (debugView == 10)
			d = vec3(rejected, 1.0 - rejected, 0.0) * (rejected > 0.0 ? 1.0 : 0.25);
		else if (debugView == 11)
			d = vec3(roughness);
		out_Color = vec4(LinearToScene(d), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}

	out_Color = vec4(LinearToScene(color), sceneHere.a);
	out_Glow = vec4(0.0);
}
