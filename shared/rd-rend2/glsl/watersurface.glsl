/*[Vertex]*/
// Modern water surface (r_waterSurface, tr_watersurface.cpp, docs/rend2-water-surface.md).
//
// The classified water surfaces of a view are drawn once each, by this program only (not their legacy
// stages), in the water slot of the main pass (RB_SubmitRenderPass): after the opaque surfaces, the
// screen-space passes and the layers up to SS_FOG, before the atmosphere / froxel fog composites and
// the blended layers. The scene under the water was copied there (color + depth, RB_WaterSurfacePrepare).
// The surface keeps the vertex deforms of its shader and the texture coordinate animation (tcMod) of its
// first stage. Resolved per-body flow separately advects the world-space detail.
in vec3 attr_Position;
in vec3 attr_Normal;
in vec2 attr_TexCoord0;
in vec2 attr_TexCoord1; // render mesh: 1 at the surface, 0 at skirt foot

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
out vec3 var_BasePosition;
out vec3 var_GeometryDisplacement;
out float var_GeometrySkirt;
out vec3 var_ShoreData;
uniform vec4 u_Water[WATER_UNIFORM_VEC4S];
uniform sampler2D u_WaterInteractionMap;

vec3 WaterShoreData(float encodedDistance)
{
	if (u_Water[34].x < 0.5 || u_Water[19].y < 0.5)
		return vec3(65504.0, 1.0, 0.0);
	float distance = max(abs(encodedDistance) - 0.25, 0.0);
	float rigid = step(encodedDistance, 0.0);
	float width = max(u_Water[34].y, 1.0);
	float profileShore = clamp(u_Water[16].w, 0.0, 1.5);
	float rigidEdge = clamp(0.03 + profileShore * 0.033333, 0.03, 0.08);
	float naturalEdge = clamp(0.15 + profileShore * 0.25, 0.15, 0.40);
	float rigidAttenuation = mix(rigidEdge, 1.0, smoothstep(0.0, width, distance));
	float naturalAttenuation = mix(naturalEdge, 1.0, smoothstep(0.0, width * 0.35, distance));
	return vec3(distance, mix(naturalAttenuation, rigidAttenuation, rigid), rigid);
}

float WaterInteractionHeight(vec3 worldPosition)
{
	if (u_Water[31].z < 0.5) return 0.0;
	vec2 uv = (worldPosition.xy - u_Water[30].xy) * u_Water[30].zw;
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 0.0;
	vec4 field = texture(u_WaterInteractionMap, u_Water[33].xy + uv * u_Water[33].zw);
	return field.a > 0.5 ? field.r : 0.0;
}

struct WaterWaveState
{
	vec3 displacement;
	vec2 slope;
	vec3 velocity;
	float height;
	float attenuation;
	float curvature;
};

WaterWaveState EvaluateWaterSurface(vec3 worldPosition, float time)
{
	WaterWaveState w;
	w.displacement = vec3(0.0);
	w.slope = vec2(0.0);
	w.velocity = vec3(0.0);
	w.height = 0.0;
	w.curvature = 0.0;
	w.attenuation = 1.0;
	vec4 commonTerms[8];
	for (int commonIndex = 0; commonIndex < 8; ++commonIndex) commonTerms[commonIndex] = u_Water[20 + commonIndex];
	WaterSurfaceCommonState commonState = EvaluateWaterSurfaceCommon(worldPosition, time, u_Water[13], u_Water[15],
		vec4(u_Water[14].x, u_Water[14].y, u_Water[17].z, u_Water[18].z), u_Water[28],
		vec4(u_Water[29].xy, u_Water[14].w, u_Water[29].w), commonTerms);
	w.displacement = commonState.displacement; w.slope = commonState.slope; w.velocity = commonState.velocity;
	w.height = commonState.height; w.attenuation = commonState.attenuation; w.curvature = commonState.curvature;
	return w;
	/* Kept below as historical source for external shader comparisons; unreachable.
	if (u_Water[13].x < 0.5)
		return w;

	// Profile speed is a character scale; one unit corresponds to 16 world units/s.
	float speed = u_Water[15].z * u_Water[13].w * 16.0;
	if (u_Water[14].w > 0.5 && u_Water[17].z > 0.0)
	{
		// Stable body-average depth. A true per-edge mask requires boundary geometry.
		w.attenuation = smoothstep(0.0, 32.0, u_Water[17].z);
	}
	// The eight deterministic components contain four macro and four medium
	// terms. Quality 0/1/2 selects 1+1, 2+2, or 4+4 of them.
	int count = u_Water[14].y < 0.5 ? 2 : (u_Water[14].y < 1.5 ? 4 : 8);
	float qualityAmplitude = count == 2 ? 1.86 : (count == 8 ? 0.59 : 1.0);
	float j00 = 1.0, j01 = 0.0, j11 = 1.0;
	for (int i = 0; i < 8; ++i)
	{
		if (i >= count) break;
		int component = count == 8 ? i : (i < count / 2 ? i : 4 + i - count / 2);
		float fi = float(component);
		vec4 term = u_Water[20 + component]; // direction.xy, amplitude, wave number
		vec2 direction = term.xy;
		float a = term.z * w.attenuation * qualityAmplitude;
		float k = term.w;
		float omega = speed * k;
		vec2 phasePosition = worldPosition.xy;
		// Flow advects only the medium band. Broad body shape remains bounded
		// and propagates under the ambient-wave profile rather than translating.
		if (u_Water[28].w > 0.5 && component >= 4)
			phasePosition -= u_Water[28].xy * time * u_Water[29].y * 0.35;
		float phase = k * dot(direction, phasePosition) - omega * time + fi * 1.37;
		float sn = sin(phase), cs = cos(phase);
		w.height += a * sn;
		w.slope += a * k * cs * direction;
		w.velocity.z -= a * omega * cs;
		w.curvature -= a * k * k * sn;
		// Bounded horizontal crest drift used by the optional render mesh.
		float horizontal = min(u_Water[14].x * u_Water[18].z * 0.12, 0.2) * a;
		w.displacement.xy += horizontal * cs * direction;
		w.velocity.xy += horizontal * omega * sn * direction;
		float jacobian = horizontal * k * sn;
		j00 -= jacobian * direction.x * direction.x;
		j01 -= jacobian * direction.x * direction.y;
		j11 -= jacobian * direction.y * direction.y;
	}
	// Convert parametric height derivatives to the normal of the displaced XY
	// surface. The horizontal drift is bounded, keeping this Jacobian invertible.
	float det = max(j00 * j11 - j01 * j01, 0.5);
	w.slope = vec2(j11 * w.slope.x - j01 * w.slope.y,
		j00 * w.slope.y - j01 * w.slope.x) / det;
	w.displacement.z = w.height;
	return w;
	*/
}


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

vec3 EvaluateWaterfallSheet(vec3 worldPosition, float time)
{
	vec3 fall = normalize(u_Water[39].xyz);
	vec3 sheetNormal = normalize(u_Water[40].xyz);
	vec3 across = normalize(cross(sheetNormal, fall));
	float along = clamp((dot(worldPosition, fall) - u_Water[41].x) * u_Water[40].w, 0.0, 1.0);
	float side = dot(worldPosition, across);
	float advected = dot(worldPosition, fall) - time * u_Water[39].w;
	float breakup = u_Water[41].y;
	float sheetWave = sin(side * 0.0107 + advected * 0.0027) +
		0.55 * sin(side * -0.023 + advected * 0.0061 + 1.7);
	float medium = sin(side * 0.061 + advected * 0.018 + 0.7) * (0.25 + 0.75 * along);
	return sheetNormal * (breakup * (2.8 * sheetWave + 1.35 * medium));
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
	var_BasePosition = wsPosition.xyz;
	var_GeometryDisplacement = vec3(0.0);
	var_GeometrySkirt = 0.0;
	var_ShoreData = WaterShoreData(attr_TexCoord1.y);
	if (u_Water[19].y > 0.5)
	{
		var_GeometrySkirt = 1.0 - attr_TexCoord1.x;
		WaterWaveState geometryWave = EvaluateWaterSurface(var_BasePosition, u_Water[5].z);
		if (u_Water[38].x > 0.5)
			geometryWave.displacement = EvaluateWaterfallSheet(var_BasePosition, u_Water[5].z);
		else
			geometryWave.displacement *= var_ShoreData.y;
		float profileShore = clamp(u_Water[16].w, 0.0, 1.5);
		float interactionEdge = mix(clamp(0.20 + profileShore * 0.20, 0.20, 0.50),
			clamp(0.05 + profileShore * 0.05, 0.05, 0.125), var_ShoreData.z);
		if (u_Water[38].x < 0.5)
			geometryWave.displacement.z += WaterInteractionHeight(var_BasePosition) * max(var_ShoreData.y, interactionEdge);
		var_GeometryDisplacement = geometryWave.displacement *
			(u_Water[19].z > 2.5 && u_Water[19].z < 3.5 ? 1.0 : attr_TexCoord1.x);
		wsPosition.xyz += var_GeometryDisplacement;
	}
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
//   [13] waves enabled, amplitude scale, wavelength scale, speed scale
//   [14] choppiness, quality, micro scale, shallow attenuation enabled
//   [15] body amplitude, wavelength, speed, micro strength
//   [16] reserved XY bounds; [17] flow XY, mean brush depth, profile ID
//   [18] debug, body ID, choppiness, wave multiplier;
//   [19] legacy deform amplitude, geometry enabled, geometry debug, foam multiplier
//   [20..27] precomputed analytic wave direction.xy, amplitude, wave number
//   [28] resolved world flow velocity xyz, enabled
//   [29] resolved speed, detail-advection scale, flow debug, packed source/confidence
//   [35] intersection foam enabled, world width, strength, debug mask
//   [36] persistent foam field enabled, debug (1 concentration, 2 velocity, 3 sources), texel world size xy
//   [37] whitewater enabled, optical strength, foam injection, debug (1 flow, 2 source, 3 result, 4 foam source)
//   [38] waterfall enabled, quality, debug, mean sheet thickness
//   [39] fall direction xyz, flow speed; [40] sheet normal xyz, inverse fall length
//   [41] top coordinate, breakup, normal scale, aeration; [42] opacity/scattering, spray, impact, profile
//
// USE_WATER_SNELL (r_waterSnell 1, a permutation: without it the prompt-1 program is unchanged): seen from inside the liquid, the surface is
// the water -> air interface (eta = ior): Snell's window is the refraction of the scene above through it
// and total internal reflection beyond the critical angle comes out of the exact Fresnel (F = 1, nothing
// transmitted), averaged over the unresolved wave slopes; no window mask. The reflected light under the
// surface is the scene in the liquid: SSR -> a cubemap captured in the liquid -> the liquid itself,
// through the medium along the reflected path.

uniform vec4 u_Water[WATER_UNIFORM_VEC4S];
uniform vec4 u_WaterPass; // x: reflection prepass, y: history / resolved reflection valid, z: history weight
uniform sampler2D u_GlowMap;

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
uniform sampler2D u_WaterInteractionMap; // RG height/velocity, B energy, A body mask
uniform sampler2D u_WaterFoamMap;        // R concentration, or RGBA sources in debug 3
uniform sampler2D u_EnvBrdfMap;
uniform samplerCube u_CubeMap;
uniform vec4 u_CubeMapInfo;
#if defined(USE_SHADOWS2)
uniform sampler2DArray u_ShadowMap;
#else
uniform sampler2DArrayShadow u_ShadowMap;
#endif

in vec3 var_BasePosition;
in vec3 var_GeometryDisplacement;
in float var_GeometrySkirt;
in vec3 var_ShoreData;
in vec3 var_Position;
in vec3 var_Normal;
in vec2 var_FlowTex;

out vec4 out_Color;
out vec4 out_Glow;
#if defined(USE_SSR)
out vec4 out_SSRNormal; // reflection prepass: receiver-to-hit vector and validity
#endif

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

// Analytic portion of EvaluateWaterSurface. The returned height, displacement,
// slope and velocity all derive from the same phases. Micro slopes are added by
// the caller from *waterWaves, whose squared moments survive mip filtering.
struct WaterWaveState
{
	vec3 displacement;
	vec2 slope;
	vec3 velocity;
	float height;
	float attenuation;
	float curvature;
};

WaterWaveState EvaluateWaterSurface(vec3 worldPosition, float time)
{
	WaterWaveState w;
	w.displacement = vec3(0.0);
	w.slope = vec2(0.0);
	w.velocity = vec3(0.0);
	w.height = 0.0;
	w.curvature = 0.0;
	w.attenuation = 1.0;
	vec4 commonTerms[8];
	for (int commonIndex = 0; commonIndex < 8; ++commonIndex) commonTerms[commonIndex] = u_Water[20 + commonIndex];
	WaterSurfaceCommonState commonState = EvaluateWaterSurfaceCommon(worldPosition, time, u_Water[13], u_Water[15],
		vec4(u_Water[14].x, u_Water[14].y, u_Water[17].z, u_Water[18].z), u_Water[28],
		vec4(u_Water[29].xy, u_Water[14].w, u_Water[29].w), commonTerms);
	w.displacement = commonState.displacement; w.slope = commonState.slope; w.velocity = commonState.velocity;
	w.height = commonState.height; w.attenuation = commonState.attenuation; w.curvature = commonState.curvature;
	return w;
	/* Historical in-file evaluator retained for source comparison; unreachable.
	if (u_Water[13].x < 0.5)
		return w;

	// Profile speed is a character scale; one unit corresponds to 16 world units/s.
	float speed = u_Water[15].z * u_Water[13].w * 16.0;
	if (u_Water[14].w > 0.5 && u_Water[17].z > 0.0)
	{
		// Stable body-average depth. A true per-edge mask requires boundary geometry.
		w.attenuation = smoothstep(0.0, 32.0, u_Water[17].z);
	}
	// The eight deterministic components contain four macro and four medium
	// terms. Quality 0/1/2 selects 1+1, 2+2, or 4+4 of them.
	int count = u_Water[14].y < 0.5 ? 2 : (u_Water[14].y < 1.5 ? 4 : 8);
	float qualityAmplitude = count == 2 ? 1.86 : (count == 8 ? 0.59 : 1.0);
	float j00 = 1.0, j01 = 0.0, j11 = 1.0;
	for (int i = 0; i < 8; ++i)
	{
		if (i >= count) break;
		int component = count == 8 ? i : (i < count / 2 ? i : 4 + i - count / 2);
		float fi = float(component);
		vec4 term = u_Water[20 + component]; // direction.xy, amplitude, wave number
		vec2 direction = term.xy;
		float a = term.z * w.attenuation * qualityAmplitude;
		float k = term.w;
		float omega = speed * k;
		vec2 phasePosition = worldPosition.xy;
		if (u_Water[28].w > 0.5 && component >= 4)
			phasePosition -= u_Water[28].xy * time * u_Water[29].y * 0.35;
		float phase = k * dot(direction, phasePosition) - omega * time + fi * 1.37;
		float sn = sin(phase), cs = cos(phase);
		w.height += a * sn;
		w.slope += a * k * cs * direction;
		w.velocity.z -= a * omega * cs;
		w.curvature -= a * k * k * sn;
		// Bounded horizontal crest drift used by the optional render mesh.
		float horizontal = min(u_Water[14].x * u_Water[18].z * 0.12, 0.2) * a;
		w.displacement.xy += horizontal * cs * direction;
		w.velocity.xy += horizontal * omega * sn * direction;
		float jacobian = horizontal * k * sn;
		j00 -= jacobian * direction.x * direction.x;
		j01 -= jacobian * direction.x * direction.y;
		j11 -= jacobian * direction.y * direction.y;
	}
	// Convert parametric height derivatives to the normal of the displaced XY
	// surface. The horizontal drift is bounded, keeping this Jacobian invertible.
	float det = max(j00 * j11 - j01 * j01, 0.5);
	w.slope = vec2(j11 * w.slope.x - j01 * w.slope.y,
		j00 * w.slope.y - j01 * w.slope.x) / det;
	w.displacement.z = w.height;
	return w;
	*/
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
	vec2 ndc = (uv - u_Water[12].xy) / u_Water[12].zw * 2.0 - 1.0;
	vec2 xy = (ndc + u_Water[4].zw) * z / u_Water[4].xy;
	return u_ViewOrigin + u_Water[7].xyz * xy.x + u_Water[8].xyz * xy.y + normalize(u_ViewForward) * z;
}

vec2 WaterProject(vec3 p)
{
	vec4 clip = u_viewProjectionMatrix * vec4(p, 1.0);
	return (clip.xy / max(clip.w, 1e-4) * 0.5 + 0.5) * u_Water[12].zw + u_Water[12].xy;
}

// render target uv of the point L along the refracted ray Rt from P, the offset scaled by
// r_waterSurfaceRefraction and bounded
vec2 WaterRefractedUV(vec3 P, vec3 Rt, float L, vec2 uv)
{
	vec2 offset = (WaterProject(P + Rt * L) - uv) * u_Water[0].w;
	float offsetLength = length(offset);
	if (offsetLength > 0.12)
		offset *= 0.12 / offsetLength;
	vec2 halfTexel = 0.5 / vec2(textureSize(u_WaterSceneMap, 0));
	vec2 lo = u_Water[12].xy + halfTexel;
	vec2 hi = u_Water[12].xy + u_Water[12].zw - halfTexel;
	// Clamping a displaced lookup repeats the last row/column into a visible
	// streak.  Fade only the unavailable part of the displacement instead;
	// samples that remain on-screen are unchanged.
	vec2 available = vec2(offset.x < 0.0 ? uv.x - lo.x : hi.x - uv.x,
		offset.y < 0.0 ? uv.y - lo.y : hi.y - uv.y);
	vec2 ratio = available / max(abs(offset), vec2(1.0e-6));
	float edgeScale = clamp(min(ratio.x, ratio.y), 0.0, 1.0);
	edgeScale *= edgeScale;
	return clamp(uv + offset * edgeScale, lo, hi);
}

// Hardware depth describes one surface, never an interpolation between surfaces.
float WaterSampleDepth(vec2 uv)
{
    ivec2 size = textureSize(u_WaterDepthMap, 0);
    ivec2 pixel = clamp(ivec2(floor(uv * vec2(size))), ivec2(0), size - 1);
    return texelFetch(u_WaterDepthMap, pixel, 0).r;
}

// depth below the surface plane (P, Ng) of the scene sample at uv (depth: its hardware depth), the
// position taken at the center of the depth texel the sample came from
float WaterDepthBelow(vec3 P, vec3 Ng, vec2 uv, float depth)
{
	vec2 size = vec2(textureSize(u_WaterDepthMap, 0));
	vec2 texel = (clamp(floor(uv * size), vec2(0.0), size - 1.0) + 0.5) / size;
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
	// This is also the exact upper edge of the roughness confidence ramp, so
	// tracing cannot contribute anything at or above it.
	if (roughness >= u_SSRSettings2.x)
		return vec4(0.0);
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

	vec4 radiance = SSRHitRadiance(vec4(hitUV, SSREncodeHitDepth(zScene), confidence),
		P, SSRConeTangent(roughness), u_SSRSettings2.z);
	return vec4(SceneToLinear(radiance.rgb / max(radiance.a, 1e-3)), radiance.a);
}


// Match the actual rounded raster viewport, including odd target dimensions.
vec2 WaterReflectionUV(vec2 sceneUV)
{
    vec2 fullSize = vec2(textureSize(u_WaterSceneMap, 0));
    vec2 traceSize = vec2(textureSize(u_SSRHistoryMap, 0));
    float scale = u_WaterPass.w > 0.0 ? u_WaterPass.w : round(fullSize.x / traceSize.x);
    scale = max(scale, 1.0);
    vec2 lo = floor(u_Water[12].xy * fullSize / scale);
    vec2 hi = ceil((u_Water[12].xy + u_Water[12].zw) * fullSize / scale);
    vec2 local = (sceneUV - u_Water[12].xy) / u_Water[12].zw;
    return (lo + local * (hi - lo)) / traceSize;
}

// Bilateral reconstruction of the reduced-resolution water reflection.
vec4 WaterResolvedSSR(vec3 P, vec3 N, float roughness, out float hitDistance)
{
	vec2 uv = WaterReflectionUV(WaterProject(P));
	vec2 size = vec2(textureSize(u_SSRHistoryMap, 0));
	vec2 st = uv * size - 0.5;
	ivec2 base = ivec2(floor(st));
	vec2 f = fract(st);
	vec4 sum = vec4(0.0);
	float weightSum = 0.0;
	float distanceSum = 0.0;
	float z = dot(P - u_ViewOrigin, normalize(u_ViewForward));
	for (int i = 0; i < 4; i++)
	{
		ivec2 offset = ivec2(i & 1, i >> 1);
		ivec2 pixel = clamp(base + offset, ivec2(0), ivec2(size) - 1);
		vec4 geom = texelFetch(u_SSRHistoryGeomMap, pixel, 0);
		if (geom.x <= 0.0)
			continue;
		vec3 normal = SSRDecodeNormal(geom.yz);
		vec2 bilinear = mix(vec2(1.0) - f, f, vec2(offset));
		float weight = bilinear.x * bilinear.y;
		weight *= exp(-abs(geom.x - z) / max(0.02 * z, 0.5));
		float normalWeight = max(dot(normal, N), 0.0);
		normalWeight *= normalWeight;
		normalWeight *= normalWeight;
		normalWeight *= normalWeight;
		normalWeight *= normalWeight;
		normalWeight *= normalWeight;
		weight *= normalWeight;
		weight *= exp(-16.0 * abs(geom.w - roughness));
		vec4 value = texelFetch(u_SSRHistoryMap, pixel, 0);
		vec4 hit = texelFetch(u_SSRPrevHitMap, pixel, 0);
		sum += value * weight;
		distanceSum += length(hit.xyz) * hit.w * weight;
		weightSum += weight;
	}
	hitDistance = distanceSum / max(weightSum, 1e-4);
	vec4 result = sum / max(weightSum, 1e-4);
	return vec4(result.rgb / max(result.a, 1e-3), result.a);
}

vec4 WaterSSR(vec3 Pw, vec3 Rw, vec3 Ngeo, vec3 Nwave, float roughness, out float hitDistance)
{
	if (u_WaterPass.y > 0.5 && u_WaterPass.x < 0.5)
		return WaterResolvedSSR(Pw, Nwave, roughness, hitDistance);
	return WaterSSRTrace(Pw, Rw, Ngeo, roughness, hitDistance);
}

void WaterReflectionPass(vec3 P, vec3 V, vec3 N, vec3 Ng, bool inside, float roughness)
{
	float z = dot(P - u_ViewOrigin, normalize(u_ViewForward));
	vec2 uv = WaterProject(P);
	float depth = WaterSampleDepth(uv);
	if (!WaterIsSky(depth) && WaterLinearDepth(depth) < z - 0.5)
		discard;
	vec3 R = reflect(-V, N);
	vec3 side = inside ? -Ng : Ng;
	float above = dot(R, side);
	if (above < 0.02)
		R = normalize(R + side * (0.02 - above));
	float distance;
	vec4 ssr = WaterSSRTrace(P, R, side, roughness, distance);
	vec4 current = vec4(ssr.rgb * ssr.a, ssr.a);
	// Receiver-relative vectors retain useful precision in RGBA16F regardless
	// of the map's absolute world coordinates.
	vec4 hit = vec4(R * distance, ssr.a > 0.0 ? 1.0 : 0.0);
	if (u_WaterPass.y > 0.5 && hit.w > 0.0)
	{
		vec4 prevClip = u_SSRReproject * vec4(P, 1.0);
		vec2 prevUV = (prevClip.xy / max(prevClip.w, 1e-4) * 0.5 + 0.5) * u_Water[12].zw + u_Water[12].xy;
		if (prevClip.w > 0.0 && SSRInsideView(prevUV))
		{
            prevUV = WaterReflectionUV(prevUV);
			vec4 geom = texture(u_SSRHistoryGeomMap, prevUV);
			vec4 oldHit = texture(u_SSRPrevHitMap, prevUV);
			vec4 old = texture(u_SSRHistoryMap, prevUV);
			float tolerance = max(0.01 * z, 0.5);
			float cone = max(2.0, distance * SSRConeTangent(roughness));
			if (geom.x > 0.0 && abs(geom.x - prevClip.w) < tolerance &&
				dot(SSRDecodeNormal(geom.yz), N) > 0.97 && abs(geom.w - roughness) < 0.05 &&
				oldHit.w > 0.0 && length(oldHit.xyz - hit.xyz) < cone)
			{
				// A changed reflected object or wave must not leave a trail. Bound
				// the history to current radiance and reduce its weight with motion.
				vec3 oldRadiance = old.rgb / max(old.a, 1e-3);
				vec3 extent = 0.05 + 0.2 * ssr.rgb;
				oldRadiance = clamp(oldRadiance, max(ssr.rgb - extent, vec3(0.0)), ssr.rgb + extent);
				float motion = length((prevUV - WaterReflectionUV(uv)) * vec2(textureSize(u_SSRHistoryMap, 0)));
				float weight = u_WaterPass.z * exp(-0.15 * motion);
				current = mix(current, vec4(oldRadiance * old.a, old.a), weight);
			}
		}
	}
	out_Color = current;
	out_Glow = vec4(z, SSREncodeNormal(N), roughness);
	out_SSRNormal = hit;
}

#endif


vec4 WaterInteractionSample(vec3 worldPosition, out vec2 worldSlope)
{
	worldSlope = vec2(0.0);
	if (u_Water[31].z < 0.5) return vec4(0.0);
	vec2 uv = (worldPosition.xy - u_Water[30].xy) * u_Water[30].zw;
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);
	vec2 atlasUV = u_Water[33].xy + uv * u_Water[33].zw;
	vec4 center = texture(u_WaterInteractionMap, atlasUV);
	if (center.a < 0.5) return center;
	ivec2 size = textureSize(u_WaterInteractionMap, 0);
	vec2 duv = 1.0 / vec2(size);
	vec2 atlasMin = u_Water[33].xy, atlasMax = u_Water[33].xy + u_Water[33].zw;
	vec4 l = texture(u_WaterInteractionMap, clamp(atlasUV - vec2(duv.x, 0.0), atlasMin, atlasMax));
	vec4 r = texture(u_WaterInteractionMap, clamp(atlasUV + vec2(duv.x, 0.0), atlasMin, atlasMax));
	vec4 d = texture(u_WaterInteractionMap, clamp(atlasUV - vec2(0.0, duv.y), atlasMin, atlasMax));
	vec4 u = texture(u_WaterInteractionMap, clamp(atlasUV + vec2(0.0, duv.y), atlasMin, atlasMax));
	// A missing neighbour is a reflecting solid wall (zero normal derivative).
	float hl = l.a > 0.5 ? l.r : center.r, hr = r.a > 0.5 ? r.r : center.r;
	float hd = d.a > 0.5 ? d.r : center.r, hu = u.a > 0.5 ? u.r : center.r;
	worldSlope = vec2((hr - hl) / (2.0 * u_Water[31].x), (hu - hd) / (2.0 * u_Water[31].y));
	return center;
}

// Depth discontinuities alone are not contacts: require an opaque sample in a
// narrow world-space slab around the water and a locally coherent surface
// whose normal crosses the water plane. This rejects shallow floors, distant
// silhouettes and the usual screen-space halo around every object.
float WaterIntersectionFoam(vec3 P, vec3 Ng, vec3 Tw, vec3 Bw, vec2 uv)
{
	if (u_Water[35].x < 0.5 || u_WaterPass.x > 0.5)
		return 0.0;
	float width = max(u_Water[35].y, 0.5);
	vec2 targetSize = vec2(textureSize(u_WaterDepthMap, 0));
	vec2 radiusUV = max(abs(WaterProject(P + Tw * width) - uv),
		abs(WaterProject(P + Bw * width) - uv));
	vec2 texel = 1.0 / targetSize;
	radiusUV = clamp(radiusUV, texel, texel * 8.0);
	float result = 0.0;
	for (int tap = 0; tap < 9; ++tap)
	{
		float angle = 6.2831853 * float(tap) / 8.0;
		vec2 sampleUV = tap == 8 ? uv : uv + vec2(cos(angle), sin(angle)) * radiusUV;
		vec2 lo = u_Water[12].xy + texel * 1.5;
		vec2 hi = u_Water[12].xy + u_Water[12].zw - texel * 1.5;
		if (any(lessThan(sampleUV, lo)) || any(greaterThan(sampleUV, hi))) continue;
		float depth = WaterSampleDepth(sampleUV);
		if (WaterIsSky(depth)) continue;
		vec3 scene = WaterWorldPosition(sampleUV, WaterLinearDepth(depth));
		float planeDistance = dot(scene - P, Ng);
		float bandConfidence = 1.0 - smoothstep(width * 0.25, width, abs(planeDistance));
		if (bandConfidence <= 0.0) continue;
		float depthX = WaterSampleDepth(sampleUV + vec2(texel.x, 0.0));
		float depthY = WaterSampleDepth(sampleUV + vec2(0.0, texel.y));
		if (WaterIsSky(depthX) || WaterIsSky(depthY)) continue;
		vec3 sceneX = WaterWorldPosition(sampleUV + vec2(texel.x, 0.0), WaterLinearDepth(depthX));
		vec3 sceneY = WaterWorldPosition(sampleUV + vec2(0.0, texel.y), WaterLinearDepth(depthY));
		vec3 dx = sceneX - scene, dy = sceneY - scene;
		vec3 opaqueNormal = cross(dx, dy);
		float normalLength = length(opaqueNormal);
		if (normalLength < 1e-4) continue;
		opaqueNormal /= normalLength;
		float crossing = 1.0 - abs(dot(opaqueNormal, Ng));
		float normalConfidence = smoothstep(0.15, 0.70, crossing);
		float depthConfidence = 1.0 - smoothstep(width * 2.0, width * 8.0,
			max(length(dx), length(dy)));
		result = max(result, bandConfidence * normalConfidence * depthConfidence);
	}
	return clamp(result, 0.0, 1.0);
}

float WaterFadeIntegral(float z)
{
	float length = 1.0 / u_Water[8].w;
	float t = clamp((z - u_Water[7].w) / length, 0.0, 1.0);
	return length * (t * t * t - 0.5 * t * t * t * t) + max(z - u_Water[7].w - length, 0.0);
}

float WaterMissingMedium(float z0, float z1)
{
	if (abs(z1 - z0) < 1e-3)
	{
		float t = clamp((z0 - u_Water[7].w) * u_Water[8].w, 0.0, 1.0);
		return t * t * (3.0 - 2.0 * t);
	}
	return clamp((WaterFadeIntegral(z1) - WaterFadeIntegral(z0)) / (z1 - z0), 0.0, 1.0);
}

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
	if (u_WaterPass.x < 0.5 && u_Water[6].y >= 0.0 && gl_FragCoord.x < u_Water[6].y)
		discard;

	vec2 uv = u_WaterPass.x > 0.5 ? WaterProject(var_Position) :
		gl_FragCoord.xy / vec2(textureSize(u_WaterSceneMap, 0));
	vec4 sceneHere = texture(u_WaterSceneMap, uv);

	float ior = u_Water[0].x;
	vec3 P = var_Position;
	vec3 toCamera = u_ViewOrigin - P;
	float cameraDistance = max(length(toCamera), 1e-3);
	vec3 V = toCamera / cameraDistance;
	vec3 Ng = normalize(var_Normal);
	bool waterfall = u_Water[38].x > 0.5;
	// A waterfall is a thin two-sided sheet, not a solid liquid half-space.
	// Face its geometric normal toward the camera so either authored winding
	// gets the same air -> sheet -> air optics.
	if (waterfall && dot(V, Ng) < 0.0)
		Ng = -Ng;
	// the normals of a liquid brush point out of the liquid: the camera behind the face is inside it
	bool inside = !waterfall && dot(V, Ng) < 0.0;

	// waves: tiling slope texture, two world space layers drifting with the wind and one layer on the
	// first stage coordinates (its tcMod scroll is the flow of the legacy water). The mips keep the
	// slope variance: distant waves become roughness instead of aliasing (LEAN).
	vec3 Tw = waterfall ? normalize(cross(Ng, normalize(u_Water[39].xyz))) :
		(abs(Ng.z) > 0.7 ? normalize(vec3(1.0, 0.0, 0.0) - Ng * Ng.x) : normalize(cross(vec3(0.0, 0.0, 1.0), Ng)));
	vec3 Bw = waterfall ? normalize(u_Water[39].xyz) : normalize(cross(Ng, Tw));
	vec2 planar = vec2(dot(P, Tw), dot(P, Bw));
	float invSize = u_Water[10].y;
	vec2 flowPlanar = vec2(0.0);
	if (u_Water[28].w > 0.5)
	{
		vec2 baseFlow = vec2(dot(u_Water[28].xyz, Tw), dot(u_Water[28].xyz, Bw)) *
			u_Water[5].z * u_Water[29].y;
		// Two close but distinct directions/speeds avoid a single conveyor belt.
		flowPlanar = baseFlow;
	}
	vec2 flow0 = vec2(0.996 * flowPlanar.x - 0.087 * flowPlanar.y,
		0.087 * flowPlanar.x + 0.996 * flowPlanar.y) * 0.82;
	vec2 flow1 = vec2(0.994 * flowPlanar.x + 0.105 * flowPlanar.y,
		-0.105 * flowPlanar.x + 0.994 * flowPlanar.y) * 1.19;
	vec4 l0 = texture(u_WaterNormalMap, (planar - flow0) * invSize + u_Water[11].xy);
	vec4 l1 = texture(u_WaterNormalMap, (planar - flow1) * (invSize * 2.37) + u_Water[11].zw);
	vec4 l2 = texture(u_WaterNormalMap, var_FlowTex * u_Water[10].x);
	float oldStrength = u_Water[0].z * 0.18;
	float strength = oldStrength * (u_Water[13].x > 0.5 ? u_Water[14].z * u_Water[15].w : 1.0);
	vec2 slope = (l0.xy + 0.6 * l1.xy + 0.5 * l2.xy) * strength;
	vec3 microNormal = normalize(Ng - slope.x * Tw - slope.y * Bw);
	WaterWaveState waves = EvaluateWaterSurface(var_BasePosition, u_Water[5].z);
	if (waterfall)
	{
		waves.displacement = vec3(0.0); waves.slope = vec2(0.0); waves.velocity = vec3(0.0);
		waves.height = 0.0; waves.curvature = 0.0; waves.attenuation = 1.0;
	}
	waves.displacement *= var_ShoreData.y;
	waves.slope *= var_ShoreData.y;
	waves.velocity *= var_ShoreData.y;
	waves.height *= var_ShoreData.y;
	waves.curvature *= var_ShoreData.y;
	waves.attenuation *= var_ShoreData.y;
	vec3 macroGradient = vec3(waves.slope, 0.0);
	vec2 macroSlope = vec2(dot(macroGradient, Tw), dot(macroGradient, Bw));
	vec2 interactionWorldSlope;
	vec4 interactionField = WaterInteractionSample(var_BasePosition, interactionWorldSlope);
	vec2 foamUV = (var_BasePosition.xy - u_Water[30].xy) * u_Water[30].zw;
	vec4 foamSample = texture(u_WaterFoamMap, clamp(foamUV, vec2(0.0), vec2(1.0)));
	float persistentFoam = u_Water[36].x > 0.5 ? clamp(foamSample.r * u_Water[19].w, 0.0, 1.0) : 0.0;
	float whitewater = u_Water[37].x > 0.5 ? clamp(foamSample.b * u_Water[37].y, 0.0, 1.0) : 0.0;
	float waterfallAlong = waterfall ? clamp((dot(var_BasePosition, normalize(u_Water[39].xyz)) - u_Water[41].x) * u_Water[40].w, 0.0, 1.0) : 0.0;
	float waterfallTurbulence = waterfall ? clamp(0.5 + 0.32 * (l0.x - l1.y) + 0.18 * l2.x, 0.0, 1.0) : 0.0;
	float waterfallThickness = waterfall ? u_Water[38].w * mix(1.0, 0.72, waterfallAlong) *
		mix(0.78, 1.22, waterfallTurbulence) : 0.0;
	float waterfallAeration = waterfall ? clamp(u_Water[41].w *
		(smoothstep(0.08, 0.92, waterfallAlong) * 0.72 + waterfallTurbulence * (0.18 + 0.35 * waterfallAlong)), 0.0, 1.0) : 0.0;
	float waterfallFoamSource = waterfall ? waterfallAeration * smoothstep(0.45, 1.0, waterfallAlong) : 0.0;
	float waterfallSpraySource = waterfall ? waterfallAeration * u_Water[42].y : 0.0;
	float waterfallImpact = waterfall ? smoothstep(0.86, 1.0, waterfallAlong) * u_Water[42].z : 0.0;
	whitewater = max(whitewater, waterfallAeration);
	vec3 interactionGradient = vec3(interactionWorldSlope, 0.0);
	float profileShore = clamp(u_Water[16].w, 0.0, 1.5);
	float interactionEdge = mix(clamp(0.20 + profileShore * 0.20, 0.20, 0.50),
		clamp(0.05 + profileShore * 0.05, 0.05, 0.125), var_ShoreData.z);
	vec2 interactionSlope = vec2(dot(interactionGradient, Tw), dot(interactionGradient, Bw)) *
		max(var_ShoreData.y, interactionEdge);
	vec2 oldSlope = (l0.xy + 0.6 * l1.xy + 0.5 * l2.xy) * oldStrength;
	vec3 oldNormal = normalize(Ng - oldSlope.x * Tw - oldSlope.y * Bw);
	if (int(u_Water[18].x + 0.5) == 8 && gl_FragCoord.x <
		(u_Water[12].x + u_Water[12].z * 0.5) * float(textureSize(u_WaterSceneMap, 0).x))
	{
		macroSlope = vec2(0.0);
		slope = oldSlope;
		strength = oldStrength;
	}
	// Aerated flow has unresolved, rapidly changing surface directions. Reuse
	// the procedural slope bands at different phases so it stays body/world
	// local and follows the resolved flow instead of scrolling in screen space.
	vec2 waterfallSlope = waterfall ? vec2((l0.x + 0.45 * l2.x) * u_Water[41].z,
		(l1.y - l0.y) * u_Water[41].z * (0.35 + 0.65 * waterfallAlong)) : vec2(0.0);
	vec2 whitewaterSlope = (l0.xy - l1.xy) * (0.38 * whitewater);
	slope += macroSlope + interactionSlope + whitewaterSlope + waterfallSlope;
	vec2 variance = (max(l0.zw - l0.xy * l0.xy, vec2(0.0)) +
		0.36 * max(l1.zw - l1.xy * l1.xy, vec2(0.0)) +
		0.25 * max(l2.zw - l2.xy * l2.xy, vec2(0.0))) * (strength * strength);
	variance += vec2(0.08 * whitewater);
	vec3 Nwater = normalize(Ng - slope.x * Tw - slope.y * Bw);
	vec3 N = inside ? -Nwater : Nwater;
	vec3 Nside = inside ? -Ng : Ng;	// geometric normal on the camera side
	float intersectionMask = !inside ? WaterIntersectionFoam(P, Ng, Tw, Bw, uv) : 0.0;
	float intersectionFoam = clamp(intersectionMask * u_Water[35].z * u_Water[19].w, 0.0, 1.0);
	int foamDebug = int(u_Water[36].y + 0.5);
	if (foamDebug > 0 && u_WaterPass.x < 0.5)
	{
		vec3 debugColor = foamDebug == 1 ? vec3(persistentFoam) :
			(foamDebug == 2 ? vec3(0.5 + 0.5 * normalize(vec3(u_Water[28].xy, 0.001)).xy, 0.15) :
			clamp(foamSample.rgb + vec3(foamSample.a, 0.5 * foamSample.a, 0.0), 0.0, 1.0));
		out_Color = vec4(LinearToScene(debugColor), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}
	int whitewaterDebug = int(u_Water[37].w + 0.5);
	if (whitewaterDebug > 0 && u_WaterPass.x < 0.5)
	{
		vec3 debugColor = vec3(0.0);
		if (whitewaterDebug == 1)
		{
			vec2 flow = length(u_Water[28].xy) > 1e-5 ? normalize(u_Water[28].xy) : vec2(0.0);
			debugColor = vec3(flow * 0.5 + 0.5, clamp(u_Water[29].x / 32.0, 0.0, 1.0));
		}
		else if (whitewaterDebug == 2) debugColor = vec3(foamSample.a, foamSample.a * 0.35, 0.0);
		else if (whitewaterDebug == 3) debugColor = vec3(whitewater);
		else debugColor = clamp(foamSample.rgb + vec3(foamSample.a, 0.5 * foamSample.a, 0.0), 0.0, 1.0);
		out_Color = vec4(LinearToScene(debugColor), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}
	if (u_Water[35].w > 0.5 && u_WaterPass.x < 0.5)
	{
		out_Color = vec4(LinearToScene(vec3(intersectionMask, intersectionMask * 0.35, 0.0)), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}

	int interactionDebug = int(u_Water[31].w + 0.5);
	if (interactionDebug > 0 && u_WaterPass.x < 0.5)
	{
		vec2 bodyUV = (var_BasePosition.xy - u_Water[30].xy) * u_Water[30].zw;
		vec3 debugColor = vec3(0.0);
		if (interactionDebug == 1) debugColor = interactionField.a > 0.5 ? vec3(bodyUV, 0.25) : vec3(0.15, 0.0, 0.0);
		else if (interactionDebug == 2) debugColor = vec3(interactionField.a);
		else if (interactionDebug == 3) debugColor = vec3(0.5 + interactionField.r / 16.0, 0.5 - abs(interactionField.r) / 16.0, 0.5 - interactionField.r / 16.0);
		else if (interactionDebug == 4) debugColor = vec3(0.5 + interactionField.g / 256.0, 0.2, 0.5 - interactionField.g / 256.0);
		else if (interactionDebug == 5) debugColor = normalize(vec3(-interactionWorldSlope, 1.0)) * 0.5 + 0.5;
		else if (interactionDebug == 6)
		{
			float ring = abs(length((bodyUV - u_Water[32].xy) / max(u_Water[32].z, 1e-4)) - 1.0);
			debugColor = ring < 0.08 ? vec3(1.0, 0.1, 0.0) : vec3(0.05);
		}
		else if (interactionDebug == 7) debugColor = vec3(interactionField.b, 0.1 * interactionField.b, 0.0);
		else if (interactionDebug == 8) debugColor = interactionField.b > 0.0 ? vec3(0.1, 1.0, 0.2) : vec3(0.08, 0.12, 0.3);
		else if (interactionDebug == 9) debugColor = vec3(fract(bodyUV * 16.0), 0.25 + 0.75 * interactionField.a);
		out_Color = vec4(LinearToScene(clamp(debugColor, 0.0, 1.0)), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}
	int shoreDebug = int(u_Water[34].w + 0.5);
	if (shoreDebug > 0 && u_WaterPass.x < 0.5)
	{
		float contact = 1.0 - smoothstep(0.0, max(u_Water[34].z, 0.25), var_ShoreData.x);
		vec3 debugColor = vec3(0.0);
		if (shoreDebug == 1) debugColor = vec3(clamp(u_Water[17].z / 128.0, 0.0, 1.0));
		else if (shoreDebug == 2) debugColor = vec3(clamp(var_ShoreData.x / max(u_Water[34].y, 1.0), 0.0, 1.0));
		else if (shoreDebug == 3) debugColor = vec3(var_ShoreData.y, var_ShoreData.y, 1.0 - var_ShoreData.y);
		else if (shoreDebug == 4) debugColor = vec3(0.5 + 0.1 * (waves.height + interactionField.r));
		else if (shoreDebug == 5) debugColor = mix(vec3(0.1, 0.75, 0.25), vec3(1.0, 0.2, 0.05), var_ShoreData.z);
		else if (shoreDebug == 6) debugColor = vec3(contact);
		else if (shoreDebug == 7) debugColor = vec3(0.05, contact, contact);
		else if (shoreDebug == 8) debugColor = vec3(interactionField.b, 0.15 * interactionField.b, 0.0);
		else debugColor = mix(vec3(0.04), vec3(1.0, 0.0, 1.0), max(contact, var_GeometrySkirt));
		out_Color = vec4(LinearToScene(clamp(debugColor, 0.0, 1.0)), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}

	// roughness: the artistic base plus the unresolved wave slopes (GGX alpha^2 ~ 2 variance)
	float baseRoughness = u_Water[0].y;
	float alpha = sqrt(min(baseRoughness * baseRoughness * baseRoughness * baseRoughness +
		(variance.x + variance.y), 1.0));
	alpha = max(alpha, 0.002);
	float roughness = sqrt(alpha);
	roughness = mix(roughness, 0.88, persistentFoam);
	roughness = mix(roughness, 0.78, whitewater);
	alpha = roughness * roughness;
	int flowDebug = int(u_Water[29].z + 0.5);
	if (flowDebug > 0)
	{
		vec3 d = vec3(0.0);
		if (flowDebug == 1)
		{
			vec3 direction = length(u_Water[28].xyz) > 1e-5 ? normalize(u_Water[28].xyz) : vec3(0.0);
			d = direction * 0.5 + 0.5;
		}
		else if (flowDebug == 2)
		{
			float magnitude = clamp(u_Water[29].x / 32.0, 0.0, 1.0);
			d = vec3(magnitude, magnitude * magnitude, 1.0 - magnitude);
		}
		else if (flowDebug == 3)
		{
			int flowCode = int(u_Water[29].w + 0.5);
			int source = flowCode / 4, confidence = flowCode - source * 4;
			d = source == 3 ? vec3(1.0, 0.8, 0.1) :
				(source == 2 ? vec3(0.1, 0.9, 1.0) :
				(source == 1 ? vec3(0.7, 0.3, 1.0) : vec3(0.08)));
			if (confidence == 1) d = mix(d, vec3(1.0, 0.2, 0.0), 0.65);
		}
		else if (flowDebug == 4)
			d = normalize(Ng - (l0.xy + 0.6 * l1.xy).x * Tw - (l0.xy + 0.6 * l1.xy).y * Bw) * 0.5 + 0.5;
		out_Color = vec4(LinearToScene(d), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}

	#if defined(USE_SSR)
	if (u_WaterPass.x > 0.5)
	{
		WaterReflectionPass(P, V, N, Ng, inside, roughness);
		return;
	}
#endif


	float NV = max(dot(N, V), 1e-4);
	float F;
	float W;
	if (!inside && WaterFlag(WATER_FLAG_ENVBRDF))
	{
		float F0 = (ior - 1.0) / (ior + 1.0);
		F0 *= F0;
		vec2 envBrdf = texture(u_EnvBrdfMap, vec2(roughness, NV)).rg;
		W = clamp(F0 * envBrdf.x + envBrdf.y, 0.0, 1.0);
		F = W;
	}
	else
	{
		F = FresnelDielectric(NV, inside ? ior : 1.0 / ior);
		W = F;
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
	float depthHere = WaterSampleDepth(uv);
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
	vec3 transmittedGlow = vec3(0.0);
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
			float hitDistance;
			vec4 ssr = WaterSSR(P, R, Ng, N, roughness, hitDistance);
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
		float depthGuess = waterfall ? waterfallThickness :
			(skyHere ? 256.0 : min(pathStraight * max(dot(V, Ng), 0.0), 2048.0));
		vec2 uvR = WaterRefractedUV(P, Rt, depthGuess / cosT, uv);
		float depthR = WaterSampleDepth(uvR);
		for (int i = 0; i < 2; i++)
		{
			if (waterfall) break;
			if (WaterIsSky(depthR))
				break;
			float h = WaterDepthBelow(P, Ng, uvR, depthR);
			if (h <= 0.0)
				break;
			uvR = WaterRefractedUV(P, Rt, h / cosT, uv);
			depthR = WaterSampleDepth(uvR);
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
				depthR = WaterSampleDepth(uvR);
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
		if (waterfall)
			pathLength = waterfallThickness * u_Water[42].x / cosT;
		else if (WaterIsSky(depthR))
			pathLength = maxPath;
		else
			pathLength = clamp(max(WaterDepthBelow(P, Ng, uvR, depthR), 0.0) / cosT, 0.0, maxPath);
		pathLength *= u_Water[1].w;

		rawRefracted = SceneToLinear(texture(u_WaterSceneMap, uvR).rgb);

		// medium: the froxel volume where it holds the liquid (fading out with the volume), the analytic
		// medium of the same parameters elsewhere
		vec3 S = vec3(0.0);
		vec3 T = vec3(1.0);
		vec3 Pstraight = skyHere ? P - V * maxPath : u_ViewOrigin + (P - u_ViewOrigin) * (zHere / zSurface);
		if (!waterfall)
			WaterFroxelSegment(P, Pstraight, S, T);
		// Remove the liquid extinction already integrated along the straight
		// view ray, then integrate the requested refracted path. The fade is
		// integrated over the entire segment, including the part beyond farZ.
		vec3 opticalDepth = sigma * pathLength;
		if (!waterfall && u_Water[2].w > 0.5)
		{
			float straightLength = length(Pstraight - P);
			float zEnd = dot(Pstraight - u_ViewOrigin, normalize(u_ViewForward));
			float coveredLength = straightLength * (1.0 - WaterMissingMedium(zSurface, zEnd));
		  #if defined(USE_FROXEL_RGB)
			opticalDepth -= sigma * coveredLength;
		  #else
			opticalDepth -= vec3(dot(sigma, vec3(1.0 / 3.0)) * coveredLength);
		  #endif
		}
		if (WaterFlag(WATER_FLAG_FOGMEDIUM))
			opticalDepth = vec3(0.0); // the map's explicit fog material defines this medium
		vec3 Ta = exp(-clamp(opticalDepth, vec3(-10.0), vec3(80.0)));
		vec3 sunIn = vec3(0.0);
		if (sun)
		{
			vec3 sunT = exp(-sigma * (0.5 * pathLength * max(dot(-Rt, Ng), 0.05) / sunCos));
			sunIn = M_PI * sunColor * sunShadow * WaterPhase(g, dot(Rt, -sunInLiquid)) * sunT;
		}
		vec3 Sa = albedo * (0.5 * ambient + sunIn) * (vec3(1.0) - Ta);
		transmittance = clamp(T * Ta, 0.0, 1.0);
		transmitted = rawRefracted * transmittance + max(S + T * Sa, vec3(0.0));
		transmittedGlow = SceneToLinear(texture(u_GlowMap, uvR).rgb) * transmittance;

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
		transmittedGlow = SceneToLinear(texture(u_GlowMap, uv).rgb);
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
			float depthR = WaterSampleDepth(uvR);
			for (int i = 0; i < 2; i++)
			{
				if (WaterIsSky(depthR))
					break;
				float h = -WaterDepthBelow(P, Ng, uvR, depthR);
				if (h <= 0.0)
					break;
				uvR = WaterRefractedUV(P, Rt, h / cosT, uv);
				depthR = WaterSampleDepth(uvR);
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
					depthR = WaterSampleDepth(uvR);
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
			transmittedGlow = SceneToLinear(texture(u_GlowMap, uvR).rgb);
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
			vec4 ssr = WaterSSR(P, R, Nside, N, roughness, hitDistance);
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
				float depthR = WaterSampleDepth(uvR);
				if (!WaterIsSky(depthR) && WaterLinearDepth(depthR) <= zSurface + 0.5)
				{
					uvR = uv;
					rejected = 1.0;
				}
			}
			rawRefracted = SceneToLinear(texture(u_WaterSceneMap, uvR).rgb);
			transmitted = rawRefracted;
			transmittedGlow = SceneToLinear(texture(u_GlowMap, uvR).rgb);
		}
		debugSource = vec3(0.0, 0.0, 1.0);
	}
#endif

	// Foam is a lit, rough scattering layer, never emission: it replaces clear
	// transmission with diffuse ambient/direct response and broadens reflection.
	float foamLight = 0.28 + 0.45 * clamp(dot(ambient, vec3(0.2126, 0.7152, 0.0722)), 0.0, 2.0);
	if (sun) foamLight += 0.28 * clamp(dot(Nwater, sunDir), 0.0, 1.0) * sunShadow;
	vec3 foamColor = vec3(0.72, 0.78, 0.80) * foamLight;
	vec3 color = (1.0 - W) * transmitted + W * reflection + glint;
	// Bulk whitewater is lit scattering inside aerated water, not emissive
	// surface paint. It suppresses clear refraction while retaining some of
	// the underlying water and reflection; persistent foam is layered later.
	vec3 whitewaterColor = vec3(0.52, 0.61, 0.64) * foamLight;
	color = mix(color, whitewaterColor, whitewater * 0.72);
	color = mix(color, foamColor, persistentFoam * 0.88);
	// Channel B of the body-local interaction field is short-lived foam laid
	// down by energetic physical splash events.  It remains surface-local,
	// follows resolved body flow and fades without a separate fluid mesh.
	float splashFoam = clamp(interactionField.b * u_Water[19].w + intersectionFoam, 0.0, 1.0);
	if (!inside && splashFoam > 0.0)
	{
		float grazing = 0.65 + 0.35 * clamp(dot(Nwater, V), 0.0, 1.0);
		vec3 transientFoamColor = vec3(0.72, 0.78, 0.80) * (0.65 + 0.35 * grazing) * foamLight;
		color = mix(color, transientFoamColor, splashFoam * 0.8);
	}

	int waterfallDebug = int(u_Water[38].z + 0.5);
	if (waterfall && waterfallDebug > 0 && u_WaterPass.x < 0.5)
	{
		vec3 d = vec3(0.0);
		if (waterfallDebug == 1) d = vec3(0.05, 0.85, 1.0);
		else if (waterfallDebug == 2) d = normalize(u_Water[39].xyz) * 0.5 + 0.5;
		else if (waterfallDebug == 3) d = vec3(waterfallAlong, 1.0 - waterfallAlong, 0.15);
		else if (waterfallDebug == 4) d = normalize(var_GeometryDisplacement + vec3(1e-5)) * 0.5 + 0.5;
		else if (waterfallDebug == 5) d = vec3(clamp(waterfallThickness / max(u_Water[38].w * 1.25, 1e-3), 0.0, 1.0));
		else if (waterfallDebug == 6) d = rawRefracted;
		else if (waterfallDebug == 7) d = vec3(waterfallTurbulence, 0.2 * waterfallTurbulence, 1.0 - waterfallTurbulence);
		else if (waterfallDebug == 8) d = vec3(waterfallAeration);
		else if (waterfallDebug == 9) d = vec3(waterfallFoamSource, waterfallFoamSource * 0.7, 0.0);
		else if (waterfallDebug == 10) d = vec3(waterfallSpraySource, 0.25 * waterfallSpraySource, 0.0);
		else if (waterfallDebug == 11) d = vec3(waterfallImpact, 0.0, 0.8 * waterfallImpact);
		else
		{
			float spacing = u_Water[38].y < 0.5 ? 128.0 : (u_Water[38].y > 1.5 ? 32.0 : 64.0);
			vec2 grid = abs(fract(planar / spacing) - 0.5) / max(fwidth(planar / spacing), vec2(1e-4));
			float line = 1.0 - smoothstep(0.0, 1.0, min(grid.x, grid.y));
			d = mix(vec3(0.025), vec3(0.1, 1.0, 0.8), line);
		}
		out_Color = vec4(LinearToScene(clamp(d, 0.0, 1.0)), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}

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
			if (waterfall)
				d = vec3(0.05, 0.85, 1.0);
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
	int waveDebug = int(u_Water[18].x + 0.5);
	int geometryDebug = int(u_Water[19].z + 0.5);
	if (geometryDebug == 2)
	{
		out_Color = vec4(LinearToScene(vec3(min(length(var_GeometryDisplacement) * 0.25, 1.0))), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}
	if (geometryDebug == 4)
	{
		vec3 bodyColor = vec3(fract(u_Water[18].y * 0.37),
			fract(u_Water[18].y * 0.61), fract(u_Water[18].y * 0.83));
		out_Color = vec4(LinearToScene(mix(bodyColor, vec3(1.0, 0.0, 0.0),
			step(0.02, var_GeometrySkirt))), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}
	if (u_Water[13].x > 0.5 && waveDebug > 0 && waveDebug != 8)
	{
		vec3 d = vec3(0.0);
		if (waveDebug == 1) d = vec3(0.5 + waves.height * 0.1);
		else if (waveDebug == 2) d = normalize(Ng - macroSlope.x * Tw - macroSlope.y * Bw) * 0.5 + 0.5;
		else if (waveDebug == 3) d = microNormal * 0.5 + 0.5;
		else if (waveDebug == 4) d = Nwater * 0.5 + 0.5;
		else if (waveDebug == 5) d = vec3(fract(u_Water[17].w * 0.17), fract(u_Water[18].y * 0.37), 0.5);
		else if (waveDebug == 6) d = vec3(waves.attenuation);
		else if (waveDebug == 7) d = vec3(min(length(waves.displacement) * 2.0, 1.0));
		else if (waveDebug == 9) d = vec3(clamp(u_Water[18].w * 0.5, 0.0, 1.0));
		else if (waveDebug == 10) d = vec3(clamp(u_Water[19].x * 0.1, 0.0, 1.0));
		out_Color = vec4(LinearToScene(d), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}

	out_Color = vec4(LinearToScene(color), sceneHere.a);
	out_Glow = vec4(LinearToScene(((1.0 - W) * transmittedGlow + glint) *
		(1.0 - persistentFoam) * (1.0 - 0.8 * whitewater)), 0.0);
}
