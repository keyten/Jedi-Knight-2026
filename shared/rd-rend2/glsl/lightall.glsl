/*[Vertex]*/
#if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
#define PER_PIXEL_LIGHTING
#endif
in vec2 attr_TexCoord0;
#if defined(USE_LIGHTMAP) || defined(USE_TCGEN)
in vec2 attr_TexCoord1;
in vec2 attr_TexCoord2;
in vec2 attr_TexCoord3;
in vec2 attr_TexCoord4;
#endif
in vec4 attr_Color;

in vec3 attr_Position;
in vec3 attr_Normal;
#if defined(PER_PIXEL_LIGHTING)
in vec4 attr_Tangent;
#endif

#if defined(USE_VERTEX_ANIMATION)
in vec3 attr_Position2;
in vec3 attr_Normal2;
in vec4 attr_Tangent2;
#elif defined(USE_SKELETAL_ANIMATION)
in uvec4 attr_BoneIndexes;
in vec4 attr_BoneWeights;
#endif

#if defined(USE_LIGHT) && !defined(USE_LIGHT_VECTOR)
in vec3 attr_LightDirection;
#endif

#if defined(USE_SILHOUETTE_POM)
in vec3 attr_Position2;	// silhouette POM shell data, see pom_silhouette.glsl
#endif

layout(std140) uniform Camera
{
	mat4 u_viewProjectionMatrix;
	vec4 u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
	// Forward+ cluster grid of this view (tr_forwardplus.cpp, CameraBlock)
	ivec4 u_FPlusGrid;    // grid texel base, light texel base, tiles x, tiles y
	vec4 u_FPlusParams;   // tile size, depth slices, slice scale, slice bias
	vec4 u_FPlusParams2;  // viewport x, viewport y, enabled, near slice distance
	vec4 u_FPlusDebug;    // r_forwardPlusDebug, selected light, max lights per cluster, r_ltcDebug
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
	vec4 u_GridAmbient[3];
	vec4 u_GridDirected[3];
	vec4 u_GridDirection[3];
	vec4 u_GridParams;
	vec4 u_GridMinimum;
	vec4 u_GridScale;
};

#if defined(USE_SKELETAL_ANIMATION)
layout(std140) uniform Bones
{
	mat3x4 u_BoneMatrices[MAX_G2_BONES];
};
#endif

#if defined(USE_DELUXEMAP)
uniform vec4   u_EnableTextures; // x = normal, y = deluxe, z = specular, w = cube
#endif

#if defined(USE_TCGEN) || defined(USE_LIGHTMAP)
uniform int u_TCGen0;
uniform vec3 u_TCGen0Vector0;
uniform vec3 u_TCGen0Vector1;
uniform int u_TCGen1;
#endif

#if defined(USE_TCMOD)
uniform vec4 u_DiffuseTexMatrix;
uniform vec4 u_DiffuseTexOffTurb;
#endif

uniform vec4 u_BaseColor;
uniform vec4 u_VertColor;
uniform vec4 u_Disintegration;
uniform int u_ColorGen;

#if defined(PER_PIXEL_LIGHTING) && defined(USE_NORMALMAP) && defined(USE_PARALLAXMAP)
uniform sampler2D u_NormalMap;
#endif

out vec4 var_TexCoords;
out vec4 var_Color;
out float var_LeafFlutter;	// r_leafFlutterDebug 8: displacement magnitude

#if defined(PER_PIXEL_LIGHTING)
out vec4 var_Normal;
out vec4 var_Tangent;
out vec4 var_ViewDir;
out vec4 var_LightDir;
#else
out vec3 var_Position;
out vec3 var_Normal;
#endif

#if defined(USE_SILHOUETTE_POM)
out vec2 var_PomShell;
flat out float var_PomHeader;
#endif

vec4 CalcColor(vec3 position)
{
	vec4 color = vec4(1.0);
	if (u_ColorGen == CGEN_DISINTEGRATION_1)
	{
		vec3 delta = u_Disintegration.xyz - position;
		float sqrDistance = dot(delta, delta);
		if (sqrDistance < u_Disintegration.w)
		{
			color = vec4(0.0);
		}
		else if (sqrDistance < u_Disintegration.w + 60.0)
		{
			color = vec4(0.0, 0.0, 0.0, 1.0);
		}
		else if (sqrDistance < u_Disintegration.w + 150.0)
		{
			color = vec4(0.435295, 0.435295, 0.435295, 1.0);
		}
		else if (sqrDistance < u_Disintegration.w + 180.0)
		{
			color = vec4(0.6862745, 0.6862745, 0.6862745, 1.0);
		}
		return color;
	}
	else if (u_ColorGen == CGEN_DISINTEGRATION_2)
	{
		vec3 delta = u_Disintegration.xyz - position;
		float sqrDistance = dot(delta, delta);
		if (sqrDistance < u_Disintegration.w)
		{
			return vec4(0.0);
		}
		return color;
	}
	return color;
}

#if defined(USE_TCGEN) || defined(USE_LIGHTMAP)
vec2 GenTexCoords(int TCGen, vec3 position, vec3 normal, vec3 TCGenVector0, vec3 TCGenVector1)
{
	vec2 tex = attr_TexCoord0;

	switch (TCGen)
	{
		case TCGEN_LIGHTMAP:
			tex = attr_TexCoord1;
		break;

		case TCGEN_LIGHTMAP1:
			tex = attr_TexCoord2;
		break;

		case TCGEN_LIGHTMAP2:
			tex = attr_TexCoord3;
		break;

		case TCGEN_LIGHTMAP3:
			tex = attr_TexCoord4;
		break;

		case TCGEN_ENVIRONMENT_MAPPED:
		{
			vec3 localOrigin = (inverse(u_ModelMatrix) * vec4(u_ViewOrigin, 1.0)).xyz;
			vec3 viewer = normalize(localOrigin - position);
			vec2 ref = reflect(viewer, normal).yz;
			tex.s = ref.x * -0.5 + 0.5;
			tex.t = ref.y *  0.5 + 0.5;
		}
		break;

		case TCGEN_ENVIRONMENT_MAPPED_SP:
		{
			vec3 localOrigin = (inverse(u_ModelMatrix) * vec4(u_ViewOrigin, 1.0)).xyz;
			vec3 viewer = normalize(localOrigin - position);
			vec2 ref = reflect(viewer, normal).xy;
			tex.s = ref.x * -0.5;
			tex.t = ref.y * -0.5;
		}
		break;

		case TCGEN_ENVIRONMENT_MAPPED_SP_FP:
		{
			vec2 ref = reflect(u_ModelLightDir.xyz, normal).xy;
			tex.s = ref.x * -0.5 + 0.5 * u_ModelLightDir.x;
			tex.t = ref.y * -0.5 + 0.5 * u_ModelLightDir.y;
		}
		break;

		case TCGEN_VECTOR:
		{
			tex = vec2(dot(position, TCGenVector0), dot(position, TCGenVector1));
		}
		break;
	}

	return tex;
}
#endif

#if defined(USE_TCMOD)
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
#endif

#if defined(USE_SKELETAL_ANIMATION)
mat4x3 GetBoneMatrix(uint index)
{
	mat3x4 bone = u_BoneMatrices[index];
	return mat4x3(
		bone[0].x, bone[1].x, bone[2].x,
		bone[0].y, bone[1].y, bone[2].y,
		bone[0].z, bone[1].z, bone[2].z,
		bone[0].w, bone[1].w, bone[2].w);
}
#endif

void main()
{
#if defined(USE_VERTEX_ANIMATION)
	vec3 position  = mix(attr_Position,    attr_Position2,    u_VertexLerp);
	vec3 normal    = mix(attr_Normal,      attr_Normal2,      u_VertexLerp);
	#if defined(PER_PIXEL_LIGHTING)
	vec3 tangent   = mix(attr_Tangent.xyz, attr_Tangent2.xyz, u_VertexLerp);
	#endif
#elif defined(USE_SKELETAL_ANIMATION)
	mat4x3 influence =
		GetBoneMatrix(attr_BoneIndexes[0]) * attr_BoneWeights[0] +
        GetBoneMatrix(attr_BoneIndexes[1]) * attr_BoneWeights[1] +
        GetBoneMatrix(attr_BoneIndexes[2]) * attr_BoneWeights[2] +
        GetBoneMatrix(attr_BoneIndexes[3]) * attr_BoneWeights[3];

    vec3 position = influence * vec4(attr_Position, 1.0);
    vec3 normal = normalize(influence * vec4(attr_Normal - vec3(0.5), 0.0));
	#if defined(PER_PIXEL_LIGHTING)
		vec3 tangent = normalize(influence * vec4(attr_Tangent.xyz - vec3(0.5), 0.0));
	#endif
#else
	vec3 position  = attr_Position;
	vec3 normal    = attr_Normal;
  #if defined(PER_PIXEL_LIGHTING)
	vec3 tangent   = attr_Tangent.xyz;
  #endif
#endif

#if !defined(USE_SKELETAL_ANIMATION)
	normal  = normal  * 2.0 - vec3(1.0);
  #if defined(PER_PIXEL_LIGHTING)
	tangent = tangent * 2.0 - vec3(1.0);
  #endif
#endif

	vec4 wsPosition = u_ModelMatrix * vec4(position, 1.0);

	// r_leafFlutter: FOLIAGE_LEAF surfaces only (leaf_flutter.glsl). The
	// object space position stays at rest for tcGen and disintegration.
	vec2 leafBands = vec2(0.0);
	float leafWeight = 0.0;
	var_LeafFlutter = 0.0;
	if (LeafFlutterEnabled())
	{
		leafWeight = LeafFlutterWeight(position);
		vec3 leafOffset = LeafFlutterOffset(wsPosition.xyz, LeafFlutterSeed(u_ModelMatrix[3].xyz),
			u_LeafFlutterParams.x, leafWeight, leafBands);
		wsPosition.xyz += leafOffset;
		var_LeafFlutter = LeafFlutterMagnitude(leafOffset);
	}

	// FOLIAGE_PLANT root bend: wind + character colliders (plant_bend.glsl).
	// The normal and tangent turn with the stem below.
	vec3 plantRest = wsPosition.xyz;
	vec3 plantRoot = vec3(0.0);
	if (PlantBendEnabled())
	{
		float plantHeat;
		plantRoot = PlantBendRoot(u_ModelMatrix);
		wsPosition.xyz = PlantBendPosition(plantRest, plantRoot, PlantBendWeight(position),
			u_PlantBendTime.x, false, plantHeat);
		var_LeafFlutter = plantHeat;
	}

#if defined(USE_TCGEN)
	vec2 texCoords = GenTexCoords(u_TCGen0, position.xyz, normal, u_TCGen0Vector0, u_TCGen0Vector1);
#else
	vec2 texCoords = attr_TexCoord0.st;
#endif

#if defined(USE_TCMOD)
	var_TexCoords.xy = ModTexCoords(texCoords, position, u_DiffuseTexMatrix, u_DiffuseTexOffTurb);
#else
	var_TexCoords.xy = texCoords;
#endif

	vec4 disintegration = CalcColor(position);

	gl_Position = u_viewProjectionMatrix * wsPosition;

	position  = wsPosition.xyz;
	normal    = normalize(mat3(u_ModelMatrix) * normal);
  #if defined(PER_PIXEL_LIGHTING)
	tangent   = normalize(mat3(u_ModelMatrix) * tangent);
  #endif
	if (LeafFlutterEnabled())
		normal = LeafFlutterNormal(normal, leafBands, leafWeight);
	if (PlantBendEnabled())
	{
		normal = FoliageRotateNormal(normal, plantRest - plantRoot, position - plantRoot);
  #if defined(PER_PIXEL_LIGHTING)
		tangent = FoliageRotateNormal(tangent, plantRest - plantRoot, position - plantRoot);
  #endif
	}

#if defined(USE_LIGHT_VECTOR)
	vec3 L = u_LocalLightOrigin.xyz;
#elif defined(PER_PIXEL_LIGHTING)
	vec3 L = attr_LightDirection * 2.0 - vec3(1.0);
	L = (u_ModelMatrix * vec4(L, 0.0)).xyz;
#endif

#if defined(USE_LIGHTMAP)
	var_TexCoords.zw = GenTexCoords(u_TCGen1, vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
#endif

	if ( u_FXVolumetricBase > 0.0 )
	{
		vec3 viewForward = u_ViewForward.xyz;

		float d = clamp(dot(normalize(viewForward), normal), 0.0, 1.0);
		d = d * d;
		d = d * d;

		var_Color = vec4(u_FXVolumetricBase * (1.0 - d));
	}
	else
	{
		var_Color = u_VertColor * attr_Color + u_BaseColor;

		#if defined(USE_LIGHT_VECTOR) && defined(USE_FAST_LIGHT)
			float sqrLightDist = dot(L, L);
			float NL = clamp(dot(normal, L) / sqrt(sqrLightDist), 0.0, 1.0);
			var_Color.rgb *= u_DirectedLight * NL + u_AmbientLight;
		#endif
	}
	var_Color *= disintegration;

#if defined(PER_PIXEL_LIGHTING)
  var_LightDir = vec4(L, 0.0);
  #if defined(USE_DELUXEMAP)
	var_LightDir -= u_EnableTextures.y * var_LightDir;
  #endif
#endif

#if defined(PER_PIXEL_LIGHTING)
	vec3 viewDir = u_ViewOrigin.xyz - position;
	var_Tangent = vec4(tangent,   (attr_Tangent.w * 2.0 - 1.0));

	#if defined(USE_NORMALMAP) && defined(USE_PARALLAXMAP)
	  vec3 bitangent = cross(normal, tangent) * var_Tangent.w;
	  mat3 TBN = mat3(tangent, bitangent, normal);
	  vec3 tangentViewDir = viewDir * TBN;

	  // normal map aspect correction for parallax mapping
	  vec2 normalSize = vec2(textureSize(u_NormalMap, 0));
	  float normalMapAspect = normalSize.y / normalSize.x;

	  tangentViewDir *= vec3(
	  	max(1.0, normalMapAspect),
	  	max(1.0, 1.0 / normalMapAspect),
	  	1.0
	  );
	#else
	  vec3 tangentViewDir = vec3(0.0);
	#endif

	// store tangent view direction in other outs to save space
	var_LightDir.w = tangentViewDir.x;
	var_Normal  = vec4(normal,    tangentViewDir.y);
	var_ViewDir = vec4(viewDir,   tangentViewDir.z);
#else
	var_Normal = normal;
	var_Position = position;
#endif

#if defined(USE_SILHOUETTE_POM)
	var_PomShell = attr_Position2.xy;
	var_PomHeader = attr_Position2.z;
#endif
}

/*[Fragment]*/
#if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
#define PER_PIXEL_LIGHTING
#endif

layout(std140) uniform Scene
{
	vec4 u_PrimaryLightOrigin;
	vec3 u_PrimaryLightAmbient;
	int  u_globalFogIndex;
	vec3 u_PrimaryLightColor;
	float u_PrimaryLightRadius;
	float u_frameTime;
	float u_deltaTime;
	// screen-space AO (tr_ao.cpp, RB_AOSceneParams)
	// x = application: 0 legacy, 1 indirect-only, 2 split (legacy left of w)
	// y = fraction of baked (lightmap/vertex) light treated as indirect
	// z = multi-bounce approximation, w = split position in window pixels
	vec4 u_AOParams;
	// x = r_debugAO (lightall views), y = specular occlusion mode (0 scalar,
	// 1 Lagarde, 2 cone), z = bent normal strength (0 = off)
	vec4 u_AOParams2;
#if defined(USE_SSGI)
	// screen-space GI source (tr_ssgi.cpp, RB_SSGISceneParams): x = source bits
	// (1 dynamic light, 2 emissive, 4 legacy glow), y = 1 linear scene / 0 legacy
	// display encoded, z = emissive scale, w = legacy glow scale
	vec4 u_SSGIParams;
#endif
};

layout(std140) uniform Camera
{
	mat4 u_viewProjectionMatrix;
	vec4 u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
	// Forward+ cluster grid of this view (tr_forwardplus.cpp, CameraBlock)
	ivec4 u_FPlusGrid;    // grid texel base, light texel base, tiles x, tiles y
	vec4 u_FPlusParams;   // tile size, depth slices, slice scale, slice bias
	vec4 u_FPlusParams2;  // viewport x, viewport y, enabled, near slice distance
	vec4 u_FPlusDebug;    // r_forwardPlusDebug, selected light, max lights per cluster, r_ltcDebug
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
	vec4 u_GridAmbient[3];
	vec4 u_GridDirected[3];
	vec4 u_GridDirection[3];
	vec4 u_GridParams;
	vec4 u_GridMinimum;
	vec4 u_GridScale;
};

struct Light
{
	vec4 origin;
	vec3 color;
	float radius;
};

layout(std140) uniform Lights
{
	uniform mat4 u_ShadowMvp;
	uniform mat4 u_ShadowMvp2;
	uniform mat4 u_ShadowMvp3;
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

uniform int u_LightMask;
// Forward+ (tr_forwardplus.cpp): light data, cluster offset / count, light indexes
uniform samplerBuffer  u_FPlusLights;
uniform usamplerBuffer u_FPlusGridMap;
uniform usamplerBuffer u_FPlusIndexMap;
#if defined(USE_LTC) && defined(PER_PIXEL_LIGHTING)
// LTC area lights (tr_arealights.cpp, tr_ltc_data.h)
uniform sampler2D u_LtcMatrixMap;    // inverse LTC matrix (m00, m02, m20, m22)
uniform sampler2D u_LtcAmplitudeMap; // norm, fresnel, 0, horizon clipped sphere form factor
#endif
uniform sampler2D u_DiffuseMap;

#if defined(USE_ENTITY_GRID) && defined(PER_PIXEL_LIGHTING)
#if defined(USE_ENTITY_GPU_GRID)
uniform sampler3D u_EntityGridAmbient;
uniform sampler3D u_EntityGridDirected;
uniform sampler3D u_EntityGridDirection;
uniform vec3 u_LightGridOrigin;
uniform vec3 u_LightGridCellInverseSize;
#endif

struct EntityGridSample
{
	vec3 ambient;
	vec3 directed;
	vec3 direction;
	float validity;
	vec3 cell;
};

#if defined(USE_ENTITY_GPU_GRID)
EntityGridSample SampleEntityGrid(vec3 position)
{
	EntityGridSample result;
	result.ambient = vec3(0.0);
	result.directed = vec3(0.0);
	result.direction = vec3(0.0);
	result.validity = 0.0;
	result.cell = (position - u_LightGridOrigin) * u_LightGridCellInverseSize;
	ivec3 bounds = textureSize(u_EntityGridAmbient, 0);
	ivec3 base = clamp(ivec3(floor(result.cell)), ivec3(0), bounds - ivec3(1));
	vec3 fraction = fract(result.cell);
	float weightSum = 0.0;
	for (int corner = 0; corner < 8; corner++)
	{
		ivec3 offset = ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
		// Match the CPU's linear BSP-array bounds check, including its edge
		// behavior when a corner crosses an X or Y row boundary.
		int linear = base.x + bounds.x * (base.y + bounds.y * base.z) +
			offset.x + bounds.x * (offset.y + bounds.y * offset.z);
		if (linear >= bounds.x * bounds.y * bounds.z)
			continue;
		ivec3 address = ivec3(linear % bounds.x,
			(linear / bounds.x) % bounds.y, linear / (bounds.x * bounds.y));
		vec3 weightAxis = vec3(offset.x != 0 ? fraction.x : 1.0 - fraction.x,
			offset.y != 0 ? fraction.y : 1.0 - fraction.y,
			offset.z != 0 ? fraction.z : 1.0 - fraction.z);
		float weight = weightAxis.x * weightAxis.y * weightAxis.z;
		vec4 encodedDirection = texelFetch(u_EntityGridDirection, address, 0);
		if (encodedDirection.a < 0.5)
			continue;
		result.ambient += weight * texelFetch(u_EntityGridAmbient, address, 0).rgb;
		result.directed += weight * texelFetch(u_EntityGridDirected, address, 0).rgb;
		result.direction += weight * (encodedDirection.rgb * 2.0 - 1.0);
		weightSum += weight;
	}
	if (weightSum > 0.0)
	{
		if (weightSum < 0.99)
		{
			result.ambient /= weightSum;
			result.directed /= weightSum;
		}
		result.validity = weightSum;
		float directionLength = length(result.direction);
		if (directionLength > 1e-5)
			result.direction /= directionLength;
	}
	if (u_GridScale.z < 0.5)
	{
		result.ambient *= ENTITY_GRID_LDR_RANGE;
		result.directed *= ENTITY_GRID_LDR_RANGE;
	}
	result.ambient *= u_GridScale.x;
	result.directed *= u_GridScale.y;
	return result;
}
#endif

EntityGridSample SampleEntityMultiPoint(float worldZ)
{
	EntityGridSample result;
	float height = clamp((worldZ - u_GridParams.x) * u_GridParams.y, 0.0, 1.0);
	float t = height < 0.5 ? clamp((height - 0.12) / 0.38, 0.0, 1.0) :
		1.0 + clamp((height - 0.5) / 0.38, 0.0, 1.0);
	int lo = t < 1.0 ? 0 : 1;
	int hi = lo + 1;
	float blend = fract(min(t, 1.99999));
	result.ambient = mix(u_GridAmbient[lo].rgb, u_GridAmbient[hi].rgb, blend);
	result.directed = mix(u_GridDirected[lo].rgb, u_GridDirected[hi].rgb, blend);
	vec3 direction = mix(u_GridDirection[lo].rgb, u_GridDirection[hi].rgb, blend);
	float directionLength = length(direction);
	result.direction = directionLength > 1e-5 ? direction / directionLength : vec3(0.0);
	result.validity = 1.0;
	result.cell = vec3(0.0);
	return result;
}

vec3 EntityGridSRGBDecode(vec3 color)
{
	color = max(color, vec3(0.0));
	vec3 lo = color * (1.0 / 12.92);
	vec3 hi = pow((color + vec3(0.055)) * (1.0 / 1.055), vec3(2.4));
	return mix(lo, hi, greaterThan(color, vec3(0.04045)));
}

vec3 EntityGridAmbientCompatibility(vec3 ambient)
{
	ambient = min(ambient + u_GridMinimum.rgb, vec3(u_GridMinimum.w));
	if (u_GridParams.w > 0.5)
		ambient = EntityGridSRGBDecode(ambient);
	return ambient;
}

vec3 EntityGridDirectedCompatibility(vec3 directed)
{
	if (u_GridParams.w > 0.5)
		directed = EntityGridSRGBDecode(directed);
	return directed;
}
#endif

#if defined(USE_LIGHTMAP)
uniform sampler2D u_LightMap;
#endif

uniform sampler2D u_EmissiveMap;

#if defined(PER_PIXEL_LIGHTING)
#if defined(USE_NORMALMAP)
uniform sampler2D u_NormalMap;
#endif

#if defined(USE_DELUXEMAP)
uniform sampler2D u_DeluxeMap;
#endif

#if defined(USE_SPECULARMAP)
uniform sampler2D u_SpecularMap;
#endif

#if defined(USE_SHADOWMAP)
#if defined(USE_SHADOWS2)
uniform sampler2DArray u_ShadowMap;
#else
uniform sampler2DArrayShadow u_ShadowMap;
#endif
#endif

#if defined(USE_SSAO)
uniform sampler2D u_SSAOMap;
#endif

#if defined(USE_DSHADOWS)
uniform sampler2DArrayShadow u_ShadowMap2;
#endif

#if defined(USE_CUBEMAP)
uniform samplerCube u_CubeMap;
uniform sampler2D u_EnvBrdfMap;
#elif defined(USE_SSR)
uniform sampler2D u_EnvBrdfMap;
#endif
#if defined(USE_DIFFUSE_IBL) && defined(USE_LIGHT_VECTOR)
uniform samplerCube u_DiffuseIrradianceMap;
uniform sampler2D u_ProbeAverageMap;
// x = blend, y = debug mode, z = zero-based probe index, w = valid probe
uniform vec4 u_DiffuseIBLParams;
#endif
#endif

// x = glow out, y = deluxe, z = screen shadow, w = cube
uniform vec4 u_EnableTextures;
// rgb = linear scale; |w| = 0 disabled, 1 explicit, 2 auto source; sign = legacy/linear scene
uniform vec4 u_EmissiveParams;

uniform vec4 u_NormalScale;
uniform vec4 u_SpecularScale;
// r_autoPBRDebug (tr_autopbr.cpp): rgb = material class / source color, a = 1 when on
uniform vec4 u_MaterialDebug;
uniform float u_LeafFlutterDebug;	// r_leafFlutterDebug 8: color by var_LeafFlutter

#if defined(USE_WETNESS) && defined(PER_PIXEL_LIGHTING)
// rain wetness, tr_weather.cpp RB_WeatherWetnessBind
uniform sampler2D u_WeatherDepthMap; // static top-down rain occlusion depth (D16)
uniform mat4 u_WeatherMvp;
uniform vec4 u_WetnessParams;  // strength (< 0: excluded draw), roughness scale, darkening, normal flattening
uniform vec4 u_WetnessParams2; // depth bias, normal offset (world), debug view, split x
uniform vec4 u_WetnessParams3; // facing floor, physical porosity (0/1), material class, unused
uniform vec4 u_PuddleParams;   // coverage (0: off, < 0: excluded draw), roughness, slope min, slope max
uniform vec4 u_PuddleParams2;  // 1 / pattern scale (world)
uniform vec4 u_PuddleHeight;   // relief depth low, 1 / (high - low) (0: no usable height), softness, fill bias
uniform vec4 u_PuddleRipple;   // slope strength (0: off), 1 / cell size (world), ring clock (cycles, mod 256), density
uniform vec4 u_RunoffParams;   // strength (0: off, < 0: excluded draw), 1 / scale (world), flow clock (cells, mod 256), probe offset (world)
uniform vec4 u_RunoffParams2;  // wind shear x, y (per unit of fall), windward amount, pattern origin z
uniform vec4 u_RunoffFrame;    // pattern frame: horizontal axis a1 (world xy), origin xy
#endif
// Runtime A/B for the standard PBR diffuse model: 0 = Lambert, 1 = Burley/Disney
uniform int u_DiffuseBRDF;
uniform float u_ParallaxBias;
#if defined(USE_PARALLAXMAP)
// tr_pom.cpp R_PomSetUniforms, docs/rend2-pom.md
uniform vec4 u_PomShadow;		// self shadow strength (0 = off), steps, start bias (depth), softness
uniform vec4 u_PomTraversal;	// adaptive steps (0 = legacy 16 + 8), min steps, max steps, binary steps
uniform vec4 u_PomLod;			// fade start, 1 / fade width (0 = no fade), self shadowed local lights (>= 256 all)
uniform vec4 u_PomDebug;		// frozen sun direction (0 = live), r_pomDebug view
#endif

#if defined(PER_PIXEL_LIGHTING) && defined(USE_CUBEMAP)
uniform vec4 u_CubeMapInfo;
#endif

#if defined(USE_ALPHA_TEST)
uniform int u_AlphaTestType;
#endif

in vec4 var_TexCoords;
in vec4 var_Color;
in float var_LeafFlutter;

#if defined(PER_PIXEL_LIGHTING)
in vec4 var_Normal;
in vec4 var_Tangent;
in vec4 var_ViewDir;
in vec4 var_LightDir;
#else
in vec3 var_Position;
in vec3 var_Normal;
#endif

#if defined(USE_SILHOUETTE_POM)
in vec2 var_PomShell;
flat in float var_PomHeader;
// texture gradients of the material maps: the foot point derivatives of the
// shell fragment (the hit coordinate jumps at the displaced silhouette)
vec2 g_pomGradX;
vec2 g_pomGradY;
#endif

out vec4 out_Color;
out vec4 out_Glow;

vec3 EmissiveLinearToLegacyScene(in vec3 color)
{
	vec3 lo = 12.92 * color;
	vec3 hi = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
	return mix(lo, hi, greaterThanEqual(color, vec3(0.0031308)));
}

vec3 EmissiveLegacySceneToLinear(in vec3 color)
{
	color = max(color, vec3(0.0));
	vec3 lo = color * (1.0 / 12.92);
	vec3 hi = pow((color + vec3(0.055)) * (1.0 / 1.055), vec3(2.4));
	return mix(lo, hi, greaterThan(color, vec3(0.04045)));
}

#if defined(USE_SSR) || defined(USE_SSGI) || defined(USE_SKIN_SSS_BUFFER)
// Screen-space attachments of renderFbo (tr_screenspace.cpp): reflections
// (tr_ssr.cpp, ssr_*.glsl), diffuse GI (tr_ssgi.cpp, ssgi_*.glsl) and skin
// scattering (tr_skinsss.cpp, skin_sss.glsl). Only written by opaque stages,
// the others have them masked.
out vec4 out_SSRNormal;   // rg = octahedral world normal, b = roughness, a = SSR receiver
#if defined(USE_SSR)
out vec4 out_SSRSpecular; // rgb = sqrt(specular IBL weight)
out vec4 out_SSRCubemap;  // rgb = cubemap reflection added to out_Color, a = view depth
#endif
#if defined(USE_SSGI)
out vec4 out_SSGIAlbedo;   // rgb = sRGB encoded diffuse albedo, a = GI receiver
out vec4 out_SSGIRadiance; // rgb = linear GI source radiance, a = view depth

// diffuse lobe of the dynamic lights of this fragment (scene space), the
// view independent part of their outgoing radiance: bounced by the SSGI
vec3 g_ssgiDynamicDiffuse = vec3(0.0);
#endif
#if defined(USE_SKIN_SSS_BUFFER)
out vec4 out_SkinDiffuse; // rgb = scattering skin diffuse (scene space), a = view depth (0 = not skin)
#endif

vec2 SSREncodeNormal(in vec3 n)
{
	n /= abs(n.x) + abs(n.y) + abs(n.z);
	vec2 e = n.xy;
	if (n.z < 0.0)
		e = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	return e * 0.5 + 0.5;
}

void SSRWriteNone(in vec3 worldPosition)
{
	float viewDepth = dot(worldPosition - u_ViewOrigin, normalize(u_ViewForward));
	out_SSRNormal = vec4(0.5, 0.5, 1.0, 0.0);
#if defined(USE_SSR)
	out_SSRSpecular = vec4(0.0);
	out_SSRCubemap = vec4(0.0, 0.0, 0.0, viewDepth);
#endif
#if defined(USE_SSGI)
	out_SSGIAlbedo = vec4(0.0);
	out_SSGIRadiance = vec4(0.0, 0.0, 0.0, viewDepth);
#endif
#if defined(USE_SKIN_SSS_BUFFER)
	out_SkinDiffuse = vec4(0.0);
#endif
}
#endif

#if defined(USE_SKIN_SSS) && defined(PER_PIXEL_LIGHTING)
// Skin scattering of the stages classified as skin (tr_skinsss.cpp).
// r_skinSSS 1: per channel wrapped diffuse lobe, red widest. A cheap
// approximation of the soft terminator, not subsurface scattering.
// r_skinSSS 2: Lambert here; the diffuse light of skin (direct, sun, dynamic
// and area lights, ambient / diffuse IBL; never specular or emission) also
// goes to out_SkinDiffuse and is diffused in screen space (skin_sss.glsl).
uniform vec4 u_SkinParams; // x = scatter of this stage (0 = not skin), y = has skin mask, z = compare split x (< 0 off)
uniform vec4 u_SkinWrap;   // rgb = wrap widths (r_skinSSS 1, else 0), w = transmission strength
uniform sampler2D u_SkinMaskMap;

float g_skinScatter = 0.0;			// scatter of this fragment (stage x mask)
vec3  g_skinWrap = vec3(0.0);		// wrap widths of this fragment, 0 = Lambert
float g_skinTransmission = 0.0;
vec3  g_skinDiffuse = vec3(0.0);	// diffuse light of this fragment, scene space

// NL of the diffuse lobe; skin in r_skinSSS 1: (NdotL + w) / (1 + w) per channel
// (not energy normalized: lit side unchanged, the terminator gains red)
vec3 SkinDiffuseNL(in float NdotL, in float NL)
{
	if (g_skinWrap.r <= 0.0)
		return vec3(NL);
	return clamp((vec3(NdotL) + g_skinWrap) / (1.0 + g_skinWrap), 0.0, 1.0);
}

// optional back light transmission (ears, fingers, r_skinSSSTransmission):
// light from behind the surface seen through it. No thickness data, so it is
// a view / light alignment and back facing falloff only.
vec3 SkinTransmission(in vec3 N, in vec3 E, in vec3 L, in vec3 light, in vec3 albedo)
{
	if (g_skinTransmission <= 0.0)
		return vec3(0.0);
	float through = pow(clamp(dot(-E, L), 0.0, 1.0), 4.0);
	float back = clamp(0.3 - dot(N, L), 0.0, 1.0);
	return light * albedo * vec3(1.0, 0.35, 0.2) * (g_skinTransmission * through * back);
}
#endif

#if defined(USE_SSGI)
// GI receiver: normal and diffuse albedo (after the metalness split: metals
// have no diffuse lobe), stored sRGB encoded for 8 bit precision
void SSGIWriteReceiver(in vec3 N, in float roughness, in vec3 albedo)
{
  #if !(defined(USE_SSR) && defined(PER_PIXEL_LIGHTING) && defined(USE_SPECULARMAP))
	out_SSRNormal = vec4(SSREncodeNormal(N), roughness, 0.0);
  #endif
	albedo = clamp(albedo, 0.0, 1.0);
	if (u_SSGIParams.y > 0.5)
		albedo = EmissiveLinearToLegacyScene(albedo);
	out_SSGIAlbedo = vec4(albedo, 1.0);
}

// GI source radiance, linear HDR. litColor = the stage color before its own
// emission, in scene space.
void SSGIWriteRadiance(in vec3 litColor, in vec3 emissiveLinear, in vec3 stageColor)
{
	int bits = int(u_SSGIParams.x);
	bool linearScene = u_SSGIParams.y > 0.5;
	vec3 radiance = vec3(0.0);
	if ((bits & 1) != 0)
	{
		// the linear share of the dynamic diffuse light in the stored color
		vec3 d = g_ssgiDynamicDiffuse;
		radiance += linearScene ? d : max(
			EmissiveLegacySceneToLinear(litColor) - EmissiveLegacySceneToLinear(litColor - d), vec3(0.0));
	}
	if ((bits & 2) != 0)
		radiance += emissiveLinear * u_SSGIParams.z;
	if ((bits & 4) != 0 && (u_EnableTextures.x > 0.5 || abs(u_EmissiveParams.w) == 2.0))
	{
		// legacy glow / auto emissive: the whole stage color, no physical intensity
		radiance += (linearScene ? max(stageColor, vec3(0.0)) : EmissiveLegacySceneToLinear(stageColor)) *
			u_SSGIParams.w;
	}
	out_SSGIRadiance.rgb = radiance;
}
#endif

#if defined(USE_SHADOWMAP) && defined(PER_PIXEL_LIGHTING)
// Legacy depth is GL_DEPTH_COMPONENT16; modern raw depth is 24-bit.
#define DEPTH_MAX_ERROR 0.0000152587890625

#if defined(USE_SHADOWS2)

struct SunCascadeResult
{
	float visibility;
	float rawDepth;
	float fixedPcf;
	float blockerDepth;
	float penumbraWorld;
	float biasWorld;
	float cascade;
};

float ShadowCascadeValue(in vec4 v, in int cascade)
{
	return cascade == 0 ? v.x : (cascade == 1 ? v.y : v.z);
}

vec3 ShadowProject(in mat4 m, in vec3 p)
{
	vec4 q = m * vec4(p, 1.0);
	return q.xyz / q.w * 0.5 + 0.5;
}

// dz / d(shadow uv), solved from screen-space derivatives. Calls are made
// before cascade-dependent branches so derivatives remain well-defined.
vec2 ShadowReceiverGradient(in vec3 shadowPos)
{
	vec2 uvDx = dFdx(shadowPos.xy);
	vec2 uvDy = dFdy(shadowPos.xy);
	float zDx = dFdx(shadowPos.z);
	float zDy = dFdy(shadowPos.z);
	float det = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
	if (abs(det) < 1e-10)
		return vec2(0.0);
	return vec2(zDx * uvDy.y - zDy * uvDx.y,
		uvDx.x * zDy - zDx * uvDy.x) / det;
}

float ShadowRawDepth(in int cascade, in vec2 uv)
{
	ivec2 size = textureSize(u_ShadowMap, 0).xy;
	ivec2 p = clamp(ivec2(uv * vec2(size)), ivec2(0), size - ivec2(1));
	return texelFetch(u_ShadowMap, ivec3(p, cascade), 0).r;
}

float ShadowReceiverDepth(in float centerDepth, in vec2 gradient,
	in vec2 uvOffset, in float depthSpan)
{
	float correction = dot(gradient, uvOffset) * u_ShadowBias.z;
	float correctionClamp = u_ShadowBias.w / max(depthSpan, 1e-5);
	correction = clamp(correction, -correctionClamp, correctionClamp);
	return centerDepth + correction - u_ShadowBias.x / max(depthSpan, 1e-5);
}

float ShadowStableAngle(in vec3 worldPosition)
{
	vec3 cell = floor(worldPosition * 0.25);
	float h = fract(sin(dot(cell, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
	return h * 6.28318530718;
}

vec2 ShadowVogel(in int sampleIndex, in int sampleCount, in float angleOffset)
{
	float i = float(sampleIndex) + 0.5;
	float r = sqrt(i / float(sampleCount));
	float a = i * 2.39996322973 + angleOffset;
	return vec2(cos(a), sin(a)) * r;
}

void ShadowSampleCounts(out int blockerSamples, out int filterSamples)
{
	int quality = int(u_ShadowPcss.w + 0.5);
	if (quality <= 0)
	{
		blockerSamples = 8;
		filterSamples = 8;
	}
	else if (quality == 1)
	{
		blockerSamples = 12;
		filterSamples = 16;
	}
	else
	{
		blockerSamples = 24;
		filterSamples = 32;
	}
}

float ShadowManualPcf(in int cascade, in vec3 shadowPos,
	in vec2 receiverGradient, in float depthSpan, in float radiusUv,
	in int sampleCount, in float angle)
{
	float visibility = 0.0;
	for (int i = 0; i < 32; ++i)
	{
		if (i >= sampleCount)
			break;
		vec2 offset = i == 0 ? vec2(0.0) :
			ShadowVogel(i - 1, sampleCount - 1, angle) * radiusUv;
		float receiver = ShadowReceiverDepth(shadowPos.z, receiverGradient, offset, depthSpan);
		visibility += receiver <= ShadowRawDepth(cascade, shadowPos.xy + offset) ? 1.0 : 0.0;
	}
	return visibility / float(sampleCount);
}

SunCascadeResult EvaluateSunCascade(in mat4 shadowMvp, in int cascade,
	in vec3 worldPosition, in vec3 geometricNormal, in float normalLight,
	in vec2 receiverGradient)
{
	SunCascadeResult result;
	float worldTexel = ShadowCascadeValue(u_ShadowTexelSize, cascade);
	float depthSpan = ShadowCascadeValue(u_ShadowDepthSpan, cascade);
	float normalOffset = worldTexel * u_ShadowBias.y * (1.0 - normalLight);
	vec3 shadowPos = ShadowProject(shadowMvp, worldPosition + geometricNormal * normalOffset);

	result.visibility = 1.0;
	result.rawDepth = 1.0;
	result.fixedPcf = 1.0;
	result.blockerDepth = 1.0;
	result.penumbraWorld = 0.0;
	result.biasWorld = u_ShadowBias.x + normalOffset;
	result.cascade = float(cascade);

	if (any(lessThan(shadowPos, vec3(0.0))) || any(greaterThan(shadowPos, vec3(1.0))))
		return result;

	int debugMode = int(u_ShadowDebug.x + 0.5);
	if (debugMode == 1 || debugMode == 7 || debugMode == 9)
		return result;
	if (debugMode == 2)
	{
		result.rawDepth = ShadowRawDepth(cascade, shadowPos.xy);
		return result;
	}

	float angle = ShadowStableAngle(worldPosition);
	int blockerSamples, filterSamples;
	ShadowSampleCounts(blockerSamples, filterSamples);
	float uvPerWorld = u_ShadowTexelSize.w / max(worldTexel, 1e-5);
	float fixedRadiusUv = 1.5 * u_ShadowTexelSize.w;
	bool pcssEnabled = u_ShadowPcss.z >= 0.5 && u_ShadowPcss.x > 0.0 && u_ShadowPcss.y > 0.0;

	if (debugMode == 3 || !pcssEnabled)
		result.fixedPcf = ShadowManualPcf(cascade, shadowPos, receiverGradient,
			depthSpan, fixedRadiusUv, filterSamples, angle);
	if (debugMode == 3)
	{
		result.visibility = result.fixedPcf;
		return result;
	}

	if (!pcssEnabled)
	{
		result.visibility = result.fixedPcf;
		return result;
	}

	// A directional light has no finite light-plane distance. Search in the
	// largest permitted receiver-space penumbra instead, which keeps the
	// meaning identical in every cascade.
	float searchWorld = max(2.0 * worldTexel, u_ShadowPcss.y);
	float searchRadiusUv = searchWorld * uvPerWorld;
	float blockerSum = 0.0;
	float blockerCount = 0.0;
	for (int i = 0; i < 24; ++i)
	{
		if (i >= blockerSamples)
			break;
		vec2 offset = i == 0 ? vec2(0.0) :
			ShadowVogel(i - 1, blockerSamples - 1, angle) * searchRadiusUv;
		float receiver = ShadowReceiverDepth(shadowPos.z, receiverGradient, offset, depthSpan);
		float sampleDepth = ShadowRawDepth(cascade, shadowPos.xy + offset);
		if (sampleDepth < receiver)
		{
			blockerSum += sampleDepth;
			blockerCount += 1.0;
		}
	}

	if (blockerCount < 0.5)
		return result;

	result.blockerDepth = blockerSum / blockerCount;
	float separationWorld = max((shadowPos.z - result.blockerDepth) * depthSpan, 0.0);
	result.penumbraWorld = min(separationWorld * u_ShadowPcss.x, u_ShadowPcss.y);
	if (debugMode == 4 || debugMode == 5)
		return result;
	float filterRadiusUv = max(0.5 * u_ShadowTexelSize.w,
		result.penumbraWorld * uvPerWorld);
	result.visibility = ShadowManualPcf(cascade, shadowPos, receiverGradient,
		depthSpan, filterRadiusUv, filterSamples, angle);
	return result;
}

SunCascadeResult MixSunCascadeResults(in SunCascadeResult a,
	in SunCascadeResult b, in float t)
{
	SunCascadeResult r;
	r.visibility = mix(a.visibility, b.visibility, t);
	r.rawDepth = mix(a.rawDepth, b.rawDepth, t);
	r.fixedPcf = mix(a.fixedPcf, b.fixedPcf, t);
	r.blockerDepth = mix(a.blockerDepth, b.blockerDepth, t);
	r.penumbraWorld = mix(a.penumbraWorld, b.penumbraWorld, t);
	r.biasWorld = mix(a.biasWorld, b.biasWorld, t);
	r.cascade = mix(a.cascade, b.cascade, t);
	return r;
}

SunCascadeResult sunShadowModern(in vec3 worldPosition,
	in vec3 geometricNormal, in float normalLight)
{
	vec3 base0 = ShadowProject(u_ShadowMvp, worldPosition);
	vec3 base1 = ShadowProject(u_ShadowMvp2, worldPosition);
	vec3 base2 = ShadowProject(u_ShadowMvp3, worldPosition);
	vec2 gradient0 = ShadowReceiverGradient(base0);
	vec2 gradient1 = ShadowReceiverGradient(base1);
	vec2 gradient2 = ShadowReceiverGradient(base2);

	float viewDepth = dot(worldPosition - u_ViewOrigin, normalize(u_ViewForward));
	float split0 = u_ShadowSplits.x;
	float split1 = u_ShadowSplits.y;
	float half0 = u_ShadowBlend.x;
	float half1 = u_ShadowBlend.y;
	SunCascadeResult result;

	if (half0 > 0.0 && viewDepth >= split0 - half0 && viewDepth <= split0 + half0)
	{
		SunCascadeResult a = EvaluateSunCascade(u_ShadowMvp, 0, worldPosition,
			geometricNormal, normalLight, gradient0);
		SunCascadeResult b = EvaluateSunCascade(u_ShadowMvp2, 1, worldPosition,
			geometricNormal, normalLight, gradient1);
		float t = smoothstep(split0 - half0, split0 + half0, viewDepth);
		result = MixSunCascadeResults(a, b, t);
	}
	else if (viewDepth < split0)
	{
		result = EvaluateSunCascade(u_ShadowMvp, 0, worldPosition,
			geometricNormal, normalLight, gradient0);
	}
	else if (half1 > 0.0 && viewDepth >= split1 - half1 && viewDepth <= split1 + half1)
	{
		SunCascadeResult a = EvaluateSunCascade(u_ShadowMvp2, 1, worldPosition,
			geometricNormal, normalLight, gradient1);
		SunCascadeResult b = EvaluateSunCascade(u_ShadowMvp3, 2, worldPosition,
			geometricNormal, normalLight, gradient2);
		float t = smoothstep(split1 - half1, split1 + half1, viewDepth);
		result = MixSunCascadeResults(a, b, t);
	}
	else if (viewDepth < split1)
	{
		result = EvaluateSunCascade(u_ShadowMvp2, 1, worldPosition,
			geometricNormal, normalLight, gradient1);
	}
	else
	{
		result = EvaluateSunCascade(u_ShadowMvp3, 2, worldPosition,
			geometricNormal, normalLight, gradient2);
	}

	float farFade = smoothstep(u_ShadowSplits.w, u_ShadowSplits.z, viewDepth);
	result.visibility = mix(result.visibility, 1.0, farFade);
	return result;
}

#else

// Input: It uses texture coords as the random number seed.
// Output: Random number: [0,1), that is between 0.0 and 0.999999... inclusive.
// Author: Michael Pohoreski
// Copyright: Copyleft 2012 :-)
// Source: http://stackoverflow.com/questions/5149544/can-i-generate-a-random-number-inside-a-pixel-shader

float random( const vec2 p )
{
  // We need irrationals for pseudo randomness.
  // Most (all?) known transcendental numbers will (generally) work.
  const vec2 r = vec2(
    23.1406926327792690,  // e^pi (Gelfond's constant)
     2.6651441426902251); // 2^sqrt(2) (Gelfond-Schneider constant)
  //return fract( cos( mod( 123456789., 1e-7 + 256. * dot(p,r) ) ) );
  return mod( 123456789., 1e-7 + 256. * dot(p,r) );
}

const vec2 poissonDisk[16] = vec2[16](
	vec2( -0.94201624, -0.39906216 ),
	vec2( 0.94558609, -0.76890725 ),
	vec2( -0.094184101, -0.92938870 ),
	vec2( 0.34495938, 0.29387760 ),
	vec2( -0.91588581, 0.45771432 ),
	vec2( -0.81544232, -0.87912464 ),
	vec2( -0.38277543, 0.27676845 ),
	vec2( 0.97484398, 0.75648379 ),
	vec2( 0.44323325, -0.97511554 ),
	vec2( 0.53742981, -0.47373420 ),
	vec2( -0.26496911, -0.41893023 ),
	vec2( 0.79197514, 0.19090188 ),
	vec2( -0.24188840, 0.99706507 ),
	vec2( -0.81409955, 0.91437590 ),
	vec2( 0.19984126, 0.78641367 ),
	vec2( 0.14383161, -0.14100790 )
);

float PCF(const sampler2DArrayShadow shadowmap, const float layer, const vec2 st, const float dist, float PCFScale)
{
	float mult;
	float scale = PCFScale / r_shadowMapSize;

#if defined(USE_SHADOW_FILTER)
	float r = random(gl_FragCoord.xy / r_FBufScale);
	float sinr = sin(r);
	float cosr = cos(r);
	mat2 rmat = mat2(cosr, sinr, -sinr, cosr) * scale;

	mult =  texture(shadowmap, vec4(st + rmat * vec2(-0.7055767, 0.196515), layer, dist));
	mult += texture(shadowmap, vec4(st + rmat * vec2(0.3524343, -0.7791386), layer, dist));
	mult += texture(shadowmap, vec4(st + rmat * vec2(0.2391056, 0.9189604), layer, dist));
  #if defined(USE_SHADOW_FILTER2)
	mult += texture(shadowmap, vec4(st + rmat * vec2(-0.07580382, -0.09224417), layer, dist));
	mult += texture(shadowmap, vec4(st + rmat * vec2(0.5784913, -0.002528916), layer, dist));
	mult += texture(shadowmap, vec4(st + rmat * vec2(0.192888, 0.4064181), layer, dist));
	mult += texture(shadowmap, vec4(st + rmat * vec2(-0.6335801, -0.5247476), layer, dist));
	mult += texture(shadowmap, vec4(st + rmat * vec2(-0.5579782, 0.7491854), layer, dist));
	mult += texture(shadowmap, vec4(st + rmat * vec2(0.7320465, 0.6317794), layer, dist));

	mult *= 0.11111;
  #else
    mult *= 0.33333;
  #endif
#else
	float r = random(gl_FragCoord.xy / r_FBufScale);
	float sinr = sin(r);
	float cosr = cos(r);
	mat2 rmat = mat2(cosr, sinr, -sinr, cosr) * scale;

	mult =  texture(shadowmap, vec4(st, layer, dist));
	for (int i = 0; i < 16; i++)
	{
		vec2 delta = rmat * poissonDisk[i];
		mult += texture(shadowmap, vec4(st + delta, layer, dist));
	}
	mult *= 1.0 / 17.0;
#endif

	return mult;
}

float sunShadow(in vec3 viewOrigin, in vec3 viewDir, in vec3 biasOffset, in sampler2DArrayShadow shadowMapCascades)
{
	vec4 biasPos = vec4(viewOrigin - viewDir + biasOffset, 1.0);
	float cameraDistance = length(viewDir);

	const float PCFScale = 1.5;
	const float edgeBias = 0.5 - ( 4.0 * PCFScale / r_shadowMapSize );
	float edgefactor = 0.0;
	const float fadeTo = 1.0;
	float result = 1.0;

	vec4 shadowpos = u_ShadowMvp * biasPos;
	shadowpos.xyz = shadowpos.xyz / shadowpos.w * 0.5 + 0.5;
	if (all(lessThanEqual(abs(shadowpos.xyz - vec3(0.5)), vec3(edgeBias))))
	{
		vec3 dCoords = smoothstep(0.3, 0.45, abs(shadowpos.xyz - vec3(0.5)));
		edgefactor = 2.0 * PCFScale * clamp(dCoords.x + dCoords.y + dCoords.z, 0.0, 1.0);
		result = PCF(shadowMapCascades,
					 0.0,
					 shadowpos.xy,
					 shadowpos.z,
					 PCFScale + edgefactor);
	}
	else
	{
		shadowpos = u_ShadowMvp2 * (biasPos + vec4(biasOffset, 0.0));
		shadowpos.xyz = shadowpos.xyz / shadowpos.w * 0.5 + 0.5;
		if (all(lessThanEqual(abs(shadowpos.xyz - vec3(0.5)), vec3(edgeBias))))
		{
			vec3 dCoords = smoothstep(0.3, 0.45, abs(shadowpos.xyz - vec3(0.5)));
			edgefactor = 0.5 * PCFScale * clamp(dCoords.x + dCoords.y + dCoords.z, 0.0, 1.0);
			result = PCF(shadowMapCascades,
						 1.0,
						 shadowpos.xy,
						 shadowpos.z,
						 PCFScale + edgefactor);
		}
		else
		{
			shadowpos = u_ShadowMvp3 * (biasPos + vec4(biasOffset, 0.0));
			shadowpos.xyz = shadowpos.xyz / shadowpos.w * 0.5 + 0.5;
			if (all(lessThanEqual(abs(shadowpos.xyz - vec3(0.5)), vec3(1.0))))
			{
				result = PCF(shadowMapCascades,
							 2.0,
							 shadowpos.xy,
							 shadowpos.z,
							 PCFScale);
				float fade = clamp(cameraDistance / r_shadowCascadeZFar * 10.0 - 9.0, 0.0, 1.0);
				result = mix(result, fadeTo, fade);
			}
		}
	}

	return result;
}
#endif
#endif

#if defined(USE_PARALLAXMAP)
#define POM_VIEW_MAX_LINEAR_STEPS 64
#define POM_VIEW_MAX_BINARY_STEPS 16
#define POM_SHADOW_MAX_STEPS 32

// State of the POM view ray hit of this fragment, shared by the self shadow
// rays of every light (GetPomSelfShadow). Filled by GetParallaxOffset or, for
// silhouette POM shells, by PomSilhouetteFragment. Height convention: the red
// channel of the normalHeightMap is the flipped height (tr_image.cpp), i.e. the
// depth s in [0, 1] below the top of the relief, 0 = top.
struct PomSurface
{
	bool  valid;
	vec2  uv;			// hit texture coordinate
	float depth;		// hit depth s
	vec3  T, B, N;		// world frame the relief is extruded along (N = up)
	vec2  scale;		// texture units per unit of s per unit of tangent slope: aspect * parallaxDepth
	vec2  gradX, gradY;	// texture gradients for every height sample
	float fade;			// distance fade, 1 = full POM
	float viewSamples;
	float shadowSamples;
};
PomSurface g_pom;
float g_pomLightWeight = 0.0;	// self shadow weight of the dynamic light being evaluated
float g_pomLocalShadow = 1.0;	// r_pomDebug 5: darkest local light self shadow

// The one reading of the material height field, shared by POM and the height
// aware puddles: the normalHeightMap alpha is flipped on load (R_FindImageFile)
// and swizzled into red (RawImage_SwizzleRA), so this is the depth below the
// top of the relief, 0 = highest point, 1 = deepest (pom_silhouette.glsl
// PomSampleDepth is the same).
float SampleMaterialDepth(in sampler2D normalMap, in vec2 uv, in vec2 gradX, in vec2 gradY)
{
	return textureGrad(normalMap, uv, gradX, gradY).r;
}

// r_pomFadeStart / r_pomFadeEnd: 1 near, 0 beyond the end (normal mapping)
float PomDistanceFade(in float viewDistance)
{
	if (u_PomLod.y <= 0.0)
		return 1.0;
	return 1.0 - clamp((viewDistance - u_PomLod.x) * u_PomLod.y, 0.0, 1.0);
}

// ordinary POM: tangent frame of the vertex shader tangentViewDir, which
// includes the normal map aspect correction
void PomInitSurface(in vec2 texCoords, in vec2 dx, in vec2 dy, in float fade)
{
	g_pom.valid = false;
	g_pom.uv = texCoords;
	g_pom.depth = 0.0;
	g_pom.N = normalize(var_Normal.xyz);
	g_pom.T = normalize(var_Tangent.xyz);
	g_pom.B = cross(g_pom.N, g_pom.T) * var_Tangent.w;
	vec2 normalSize = vec2(textureSize(u_NormalMap, 0));
	float normalMapAspect = normalSize.y / normalSize.x;
	vec2 aspect = vec2(max(1.0, normalMapAspect), max(1.0, 1.0 / normalMapAspect));
	g_pom.scale = aspect * (u_NormalScale.a * fade);
	g_pom.gradX = dx;
	g_pom.gradY = dy;
	g_pom.fade = fade;
	g_pom.viewSamples = 0.0;
	g_pom.shadowSamples = 0.0;
}

// linearSearchSteps / binarySearchSteps: 16 / 8 is the legacy traversal
float RayIntersectDisplaceMap(in vec2 inDp, in vec2 ds, in sampler2D normalMap, in float parallaxBias,
	in int linearSearchSteps, in int binarySearchSteps, in vec2 dx, in vec2 dy)
{
	vec2 dp = fract(inDp - parallaxBias * ds);

	// current size of search window
	float size = 1.0 / float(linearSearchSteps);

	// current depth position
	float depth = 0.0;

	// best match found (starts with last position 1.0)
	float bestDepth = 1.0;

	// try sampling at least one border pixel
	vec2 tMin = (vec2(0.0) - dp) / ds;
	vec2 tMax = (vec2(1.0) - dp) / ds;
	vec2 t = max(tMin, tMax);
	float tExit  = min(t.x, t.y);
	float stepFraction = fract(tExit / size) * size;
	depth -= size-stepFraction;

	// search front to back for first point inside object
	for(int i = 0; i < POM_VIEW_MAX_LINEAR_STEPS; ++i)
	{
		if (i >= linearSearchSteps)
			break;
		depth += size;

		// height is flipped before uploaded to the gpu
		float t = SampleMaterialDepth(normalMap, dp + ds * depth, dx, dy);
		g_pom.viewSamples += 1.0;

		if(depth >= t)
		{
			bestDepth = depth;	// store best depth
			break;
		}
	}

	depth = bestDepth;

	// recurse around first point (depth) for closest match
	for(int i = 0; i < POM_VIEW_MAX_BINARY_STEPS; ++i)
	{
		if (i >= binarySearchSteps)
			break;
		size *= 0.5;

		// height is flipped before uploaded to the gpu
		float t = SampleMaterialDepth(normalMap, dp + ds * depth, dx, dy);

		if(depth >= t)
		{
			bestDepth = depth;
			depth -= 2.0 * size;
		}

		depth += size;
	}
	g_pom.viewSamples += float(binarySearchSteps) + 2.0;

	float beforeDepth = SampleMaterialDepth(normalMap, dp + ds * (depth-size), dx, dy) - depth + size;
	float afterDepth  = SampleMaterialDepth(normalMap, dp + ds * depth, dx, dy) - depth;
	float deltaDepth = beforeDepth - afterDepth;
	float weight = mix(0.0, beforeDepth / deltaDepth , deltaDepth > 0);
	bestDepth += weight*size;

	// the virtual hit, for the self shadow rays
	g_pom.uv = dp + ds * bestDepth;
	g_pom.depth = bestDepth;

	return bestDepth - parallaxBias;
}

// Self shadowing of direct light by the relief (r_pomSelfShadow): marches
// from the view ray hit towards the light, L = world direction towards the
// light (normalized). Soft visibility from the deepest occluder penetration
// along the ray, nearer occluders count more. 1 = lit. Only for direct light:
// the callers multiply the sun shadow and the dynamic light attenuation,
// never ambient, lightmap ambient, IBL, SSR, SSGI or emissive.
// Compiled only with r_pomSelfShadow set at renderer start
// (USE_POM_SELFSHADOW): the rays are inlined at every call site.
float GetPomSelfShadow(in vec3 L)
{
#if !defined(USE_POM_SELFSHADOW)
	return 1.0;
#else
	float strength = clamp(u_PomShadow.x, 0.0, 1.0) * g_pom.fade;
	if (!g_pom.valid || strength <= 0.0)
		return 1.0;

	vec3 Lt = vec3(dot(L, g_pom.T), dot(L, g_pom.B), dot(L, g_pom.N));
	// towards the base plane every ray ends in the relief: fade to full
	// shadow below 3 degrees instead of marching nearly horizontal rays
	const float minElevation = 0.05;
	float horizon = clamp(Lt.z / minElevation, 0.0, 1.0);
	float visibility = 0.0;
	float s0 = g_pom.depth - u_PomShadow.z;
	if (horizon > 0.0)
	{
		visibility = 1.0;
		if (s0 > 0.0)
		{
			// texture offset per unit of s climbed towards the light
			vec2 duv = Lt.xy / max(Lt.z, minElevation) * g_pom.scale;
			int steps = int(u_PomShadow.y);
			float invSteps = 1.0 / float(steps);
			float occlusion = 0.0;
			for (int i = 0; i < POM_SHADOW_MAX_STEPS; i++)
			{
				if (i >= steps || occlusion >= 1.0)
					break;
				float f = (float(i) + 0.5) * invSteps;
				float s = s0 * (1.0 - f);
				// height is flipped before uploaded to the gpu
				float h = SampleMaterialDepth(u_NormalMap, g_pom.uv + duv * (g_pom.depth - s), g_pom.gradX, g_pom.gradY);
				g_pom.shadowSamples += 1.0;
				occlusion = max(occlusion, (s - h) * u_PomShadow.w * (1.0 - f));
			}
			visibility = 1.0 - clamp(occlusion, 0.0, 1.0);
		}
		visibility *= horizon;
	}
	return mix(1.0, visibility, strength);
#endif
}

// r_pomDebugFreezeLight keeps the sun direction of the moment it was set
vec3 PomSunDirection(in vec3 primaryLightDir)
{
	return dot(u_PomDebug.xyz, u_PomDebug.xyz) > 0.0 ? normalize(u_PomDebug.xyz) : primaryLightDir;
}

vec3 PomDebugHeat(in float x)
{
	x = clamp(x, 0.0, 1.0);
	return clamp(vec3(1.5 - abs(4.0 * x - 3.0), 1.5 - abs(4.0 * x - 2.0), 1.5 - abs(4.0 * x - 1.0)), 0.0, 1.0);
}
#endif

vec2 GetParallaxOffset(in vec2 texCoords, in vec3 tangentDir)
{
#if defined(USE_PARALLAXMAP)
	vec2 dx = dFdx(texCoords);
	vec2 dy = dFdy(texCoords);
	float fade = PomDistanceFade(length(var_ViewDir.xyz));
	PomInitSurface(texCoords, dx, dy, fade);
	if (fade <= 0.0)
		return vec2(0.0);

	vec3 offsetDir = normalize(tangentDir);

	// r_pomAdaptiveSteps: more linear steps towards grazing angles, fewer
	// with the distance fade; off = the legacy 16 + 8 traversal
	int linearSteps = 16;
	int binarySteps = 8;
	if (u_PomTraversal.x > 0.0)
	{
		float grazing = 1.0 - abs(offsetDir.z);
		float steps = mix(u_PomTraversal.y, u_PomTraversal.z, grazing);
		steps = mix(min(4.0, steps), steps, fade);
		linearSteps = int(steps + 0.5);
		binarySteps = int(u_PomTraversal.w);
	}

	offsetDir.xy *= -u_NormalScale.a / offsetDir.z;
	offsetDir.xy *= fade;

	vec2 offset = offsetDir.xy * RayIntersectDisplaceMap(texCoords, offsetDir.xy, u_NormalMap, u_ParallaxBias,
		linearSteps, binarySteps, dx, dy);
	g_pom.valid = true;
	return offset;
#else
	return vec2(0.0);
#endif
}

float D_Charlie(in float a, in float NH)
{
	// Estevez and Kulla 2017, "Production Friendly Microfacet Sheen BRDF"
	float invAlpha = 1.0 / a;
	float cos2h = NH * NH;
	float sin2h = max(1.0 - cos2h, 0.0078125); // 2^(-14/2), so sin2h^2 > 0 in fp16
	return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * M_PI);
}

float V_Neubelt(in float NV, in float NL)
{
	// Neubelt and Pettineo 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"
	return 1.0 / (4.0 * (NL + NV - NL * NV));
}

float D_Ashikhmin(float roughness, float nh){
                float a2 = roughness * roughness;
                float cos2h = nh * nh ;
                float sin2h = max(1.0 - cos2h, 0.0078125); // 2^(-14/2), so sin2h^2 > 0 in fp16
	            float sin4h = sin2h * sin2h;
                float cot2 = -cos2h / (a2 * sin2h);
	            return 1.0 / (M_PI * (4.0 * a2 + 1.0) * sin4h) * (4.0 * exp(cot2) + sin4h);

            }

vec3 Specular_CharlieSheen(float Roughness, float NoH, float NoV, float NoL, vec3 SpecularColor, float cloth)
{
	float D = cloth > 0.f ? D_Ashikhmin(Roughness, NoH) : D_Charlie(Roughness, NoH);

	return (D * V_Neubelt(NoV, NoL)) * SpecularColor; //No fresnel in the documentation.
}

vec3 Fresnel_Schlick(const vec3 f0, float f90, float VoH)
{
	// Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"
	return f0 + (f90 - f0) * pow(1.0 - VoH, 5.f);
}

vec3 Diff_Burley(float roughness, float NoV, float NoL, float LoH)
{
	// Burley 2012, "Physically-Based Shading at Disney"
	float f90 = 0.5 + 2.0 * roughness * LoH * LoH;
	vec3 lightScatter = Fresnel_Schlick(vec3(1.0), f90, NoL);
	vec3 viewScatter = Fresnel_Schlick(vec3(1.0), f90, NoV);
	return lightScatter * viewScatter * (1.0 / M_PI);
}

vec3 F_Schlick(in vec3 SpecularColor, in float VH)
{
	float Fc = pow(1 - VH, 5);
	return clamp(50.0 * SpecularColor.g, 0.0, 1.0) * Fc + (1 - Fc) * SpecularColor; //hacky way to decide if reflectivity is too low (< 2%)
}

float D_GGX( in float NH, in float a )
{
	/*float alphaSq = roughness*roughness;
	float f = (NH * alphaSq - NH) * NH + 1.0;
	return alphaSq / (f * f);*/

	float a2 = a * a;
	float d = (NH * a2 - NH) * NH + 1;
	return a2 / (M_PI * d * d);
}

// Appoximation of joint Smith term for GGX
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float V_SmithJointApprox(in float a, in float NV, in float NL)
{
	float Vis_SmithV = NL * (NV * (1 - a) + a);
	float Vis_SmithL = NV * (NL * (1 - a) + a);
	return 0.5 * (1.0 / (Vis_SmithV + Vis_SmithL));
}

float CalcVisibility(in float NL, in float NE, in float roughness)
{
	float alphaSq = roughness * roughness;

	float lambdaE = NL * sqrt((-NE * alphaSq + NE) * NE + alphaSq);
	float lambdaL = NE * sqrt((-NL * alphaSq + NL) * NL + alphaSq);

	return 0.5 / (lambdaE + lambdaL);
}

// http://www.frostbite.com/2014/11/moving-frostbite-to-pbr/
vec3 CalcSpecular(
	in vec3 specular,
	in float NH,
	in float NL,
	in float NE,
	in float LH,
	in float VH,
	in float roughness
)
{
	//Using #if to define our BRDF's is a good idea.
#if !defined(USE_CLOTH_BRDF) //should define this as the base BRDF
	vec3  F = F_Schlick(specular, VH);
	float D = D_GGX(NH, roughness);
	float V = V_SmithJointApprox(roughness, NE, NL);
#else //and define this as the cloth BRDF
	//this cloth model essentially uses the metallic input to help transition from isotropic to anisotropic reflections.
	//as cloth is a microfibre structure, cloth like velevet and silk tends to have anisotropy.
	vec3 F = specular; //this shading model omits fresnel
	float D = D_Charlie(roughness, NH);
	float V = V_Neubelt(NE, NL);
#endif

	return D * F * V;
}

//Energy conserving wrap term.
float WrapLambert(in float NL, in float w)
{
	return clamp((NL + w) / pow(1.0 + w, 2.0), 0.0, 1.0);
}

vec3 Diffuse_Lambert(in vec3 DiffuseColor)
{
	return DiffuseColor * (1.0 / M_PI);
}

vec3 CalcDiffuse(
	in vec3 diffuse,
	in float NE,
	in float NL,
	in float LH,
	in float roughness
)
{
	//Using #if to define our diffuse's is a good idea.
#if !defined(USE_CLOTH_BRDF) //should define this as the base BRDF
	if (u_DiffuseBRDF == 1)
	{
		return diffuse * Diff_Burley(roughness, clamp(NE, 0.0, 1.0), NL, LH);
	}
	return Diffuse_Lambert(diffuse);
#else //and define this as the cloth diffuse
	//this cloth model has a wrapped diffuse, we can be energy conservant here.
	vec3 d = Diffuse_Lambert(diffuse);
	d *= WrapLambert(NL, 0.5);
	// Cheap subsurface scatter
	// ideally we should actually have a new colour for subsurface, but for cloth most times it makes sense to just use the diffuse.
	d *= clamp(diffuse + NL, 0.0, 1.0);
	return d;
#endif
}

float CalcLightAttenuation(float normDist)
{
	// zero light at 1.0, approximating q3 style
	float attenuation = 0.5 * normDist - 0.5;
	return clamp(attenuation, 0.0, 1.0);
}

#if defined(USE_DSHADOWS)
#define DEPTH_MAX_ERROR 0.0000152587890625

vec2 poissonDiscPolar[9] = vec2[9]
(
vec2(-0.7055767, 0.196515),    vec2(0.3524343, -0.7791386),
vec2(0.2391056, 0.9189604),    vec2(-0.07580382, -0.09224417),
vec2(0.5784913, -0.002528916), vec2(0.192888, 0.4064181),
vec2(-0.6335801, -0.5247476),  vec2(-0.5579782, 0.7491854),
vec2(0.7320465, 0.6317794)
);

// based on https://www.gamedev.net/forums/topic/687535-implementing-a-cube-map-lookup-function/5337472/
vec3 sampleCube(in vec3 v)
{
	vec3 vAbs = abs(v);
	float ma = 0.0;
	vec2 uv = vec2(0.0);
	float faceIndex = 0.0;
	if(vAbs.z >= vAbs.x && vAbs.z >= vAbs.y)
	{
		faceIndex = v.z < 0.0 ? 5.0 : 4.0;
		ma = 0.5 / vAbs.z;
		uv = vec2(v.z < 0.0 ? -v.x : v.x, -v.y);
	}
	else if(vAbs.y >= vAbs.x)
	{
		faceIndex = v.y < 0.0 ? 3.0 : 2.0;
		ma = 0.5 / vAbs.y;
		uv = vec2(v.x, v.y < 0.0 ? -v.z : v.z);
	}
	else
	{
		faceIndex = v.x < 0.0 ? 1.0 : 0.0;
		ma = 0.5 / vAbs.x;
		uv = vec2(v.x < 0.0 ? v.z : -v.z, -v.y);
	}
	return vec3(uv * ma + 0.5, faceIndex);
}

float pcfShadow(in sampler2DArrayShadow depthMap, in vec3 L, in float distance, in int lightId)
{
	const int samples = 9;
	const float diskRadius = M_PI / 512.0;

	vec2 polarL = vec2(atan(L.z, L.x), acos(L.y));
	float shadow = 0.0;

	for (int i = 0; i < samples; ++i)
	{
		vec2 samplePolar = poissonDiscPolar[i] * diskRadius + polarL;
		vec3 sampleVec = vec3(0.0);
		sampleVec.x = cos(samplePolar.x) * sin(samplePolar.y);
		sampleVec.z = sin(samplePolar.x) * sin(samplePolar.y);
		sampleVec.y = cos(samplePolar.y);

		vec3 lookup = sampleCube(sampleVec) + vec3(0.0, 0.0, lightId * 6.0);

		shadow += texture(depthMap, vec4(lookup, distance));
	}
	shadow /= float(samples);
	return shadow;
}

float getLightDepth(in vec3 Vec, in float f)
{
	vec3 AbsVec = abs(Vec);
	float Z = max(AbsVec.x, max(AbsVec.y, AbsVec.z));

	const float n = 1.0;

	float NormZComp = (f + n) / (f - n) - 2 * f*n / (Z* (f - n));

	return ((NormZComp + 1.0) * 0.5);
}
#endif

/*
Dynamic lights. EvaluateDynamicLight / EvaluateDynamicLightSimple are the one
place where the radiance of a single dynamic light is computed. The legacy loop
(u_LightMask bits over the Lights block) and the Forward+ loop (cluster light
list, tr_forwardplus.cpp) only differ in which lights they iterate.
*/

// Forward+: the cluster of this fragment and its light list
#define FPLUS_HARD_CAP 256		// guards the loop against corrupted counts
#define FPLUS_LIGHT_TEXELS 5

// area light types / flags, tr_local.h DLIGHT_* / AREALIGHT_*
#define FPLUS_TYPE_RECT 1.0
#define FPLUS_TYPE_LINE 2.0
#define AREALIGHT_TWO_SIDED     1
#define AREALIGHT_SPECULAR_ONLY 2
#define AREALIGHT_DYNAMIC       4
#define AREALIGHT_SELECTED      8

struct FPlusLight
{
	vec3  origin;		// area lights: centre
	float radius;		// area lights: influence range
	vec3  color;		// area lights: radiance
	float type;			// 0 = point, FPLUS_TYPE_*
	int   shadowSlot;	// < 0 = unshadowed
	int   flags;		// area lights: AREALIGHT_*
	float halfWidth;	// area lights: along right (line: half length)
	float halfHeight;	// area lights: along up (line: tube radius)
};

bool FPlusEnabled()
{
	return u_FPlusParams2.z > 0.0;
}

// must match R_ForwardPlusSlice (tr_forwardplus.cpp)
int FPlusSlice(in vec3 position)
{
	int numSlices = int(u_FPlusParams.y);
	if (numSlices <= 1)
		return 0;
	float depth = dot(position - u_ViewOrigin, normalize(u_ViewForward));
	if (depth <= u_FPlusParams2.w)
		return 0;
	return clamp(1 + int(floor(log(depth) * u_FPlusParams.z + u_FPlusParams.w)), 1, numSlices - 1);
}

ivec2 FPlusTile()
{
	ivec2 tile = ivec2((gl_FragCoord.xy - u_FPlusParams2.xy) / u_FPlusParams.x);
	return clamp(tile, ivec2(0), u_FPlusGrid.zw - ivec2(1));
}

int FPlusCluster(in vec3 position)
{
	ivec2 tile = FPlusTile();
	return (FPlusSlice(position) * u_FPlusGrid.w + tile.y) * u_FPlusGrid.z + tile.x;
}

// x = first entry in the index list, y = light count
ivec2 FPlusClusterLights(in vec3 position)
{
	uvec4 cell = texelFetch(u_FPlusGridMap, u_FPlusGrid.x + FPlusCluster(position));
	return ivec2(int(cell.x), min(int(cell.y), FPLUS_HARD_CAP));
}

int FPlusLightIndex(in int entry)
{
	return int(texelFetch(u_FPlusIndexMap, entry).x);
}

FPlusLight FPlusFetchLight(in int lightIndex)
{
	int base = u_FPlusGrid.y + lightIndex * FPLUS_LIGHT_TEXELS;
	vec4 t0 = texelFetch(u_FPlusLights, base);
	vec4 t1 = texelFetch(u_FPlusLights, base + 1);
	vec4 t2 = texelFetch(u_FPlusLights, base + 2);
	FPlusLight light;
	light.origin = t0.xyz;
	light.radius = t0.w;
	light.color = t1.rgb;
	light.type = t1.w;
	light.shadowSlot = int(t2.x);
	light.flags = int(t2.y);
	light.halfWidth = t2.z;
	light.halfHeight = t2.w;
	return light;
}

// r_forwardPlusDebug 6 / 7 / 9 only show some lights
// debug views are compiled only with r_forwardPlusDebug set at shader load
// (USE_FPLUS_DEBUG): in every lightall permutation they cost compile time
bool FPlusDebugSkipLight(in FPlusLight light, in int lightIndex)
{
#if !defined(USE_FPLUS_DEBUG)
	return false;
#else
	int mode = int(u_FPlusDebug.x);
	if (mode == 6)
		return light.shadowSlot < 0;
	if (mode == 7)
		return light.shadowSlot >= 0;
	if (mode == 9)
		return lightIndex != int(u_FPlusDebug.y);
	return false;
#endif
}

#if defined(PER_PIXEL_LIGHTING)
struct DLightSurface
{
	vec3  position;
	vec3  N;
	vec3  E;
	float NE;
	vec3  diffuse;
	vec3  specular;
	float roughness;
	vec3  vertexNormal;
};

// receiver side visibility of one light, 1 = not occluded: POM self
// shadowing (tr_pom.cpp) for the lights the budget picked, legacy and
// Forward+ alike; it scales the attenuation before the SSGI source is taken
float DynamicLightReceiverVisibility(in DLightSurface s, in vec3 L)
{
#if defined(USE_PARALLAXMAP)
	if (g_pomLightWeight > 0.0)
	{
		float visibility = mix(1.0, GetPomSelfShadow(L), g_pomLightWeight);
		g_pomLocalShadow = min(g_pomLocalShadow, visibility);
		return visibility;
	}
#endif
	return 1.0;
}

#if defined(USE_PARALLAXMAP)
// estimated contribution of a light at the receiver, for the self shadow budget
float PomLightImportance(in vec3 toLight, in vec3 lightColor, in float lightRadius)
{
	float attenuation = CalcLightAttenuation(lightRadius * lightRadius / max(dot(toLight, toLight), 1e-6));
	return dot(lightColor, vec3(0.2126, 0.7152, 0.0722)) * attenuation;
}

// r_pomSelfShadowLights 1 / 2: only the N strongest lights at this pixel get a
// self shadow ray. Returns the importance of the (N+1)-th strongest light; the
// weight of a light fades in between 1x and 1.5x of it, so the choice changes
// without pops. 0 = every light, < 0 = none.
float PomLocalLightCut(in vec3 position, in bool fplus, in ivec2 list)
{
#if !defined(USE_POM_SELFSHADOW)
	return -1.0;
#else
	int maxLights = int(u_PomLod.z);
	if (clamp(u_PomShadow.x, 0.0, 1.0) * g_pom.fade <= 0.0 || !g_pom.valid || maxLights <= 0)
		return -1.0;
	if (maxLights >= list.y)
		return 0.0;
	maxLights = min(maxLights, 4);

	float top[5] = float[5](0.0, 0.0, 0.0, 0.0, 0.0);
	for (int k = 0; k < list.y; k++)
	{
		vec3 lightOrigin, lightColor;
		float lightRadius;
		if (fplus)
		{
			FPlusLight light = FPlusFetchLight(FPlusLightIndex(list.x + k));
			if (light.type != 0.0)
				continue;
			lightOrigin = light.origin;
			lightColor = light.color;
			lightRadius = light.radius;
		}
		else
		{
			if ( ( u_LightMask & ( 1 << k ) ) == 0 )
				continue;
			lightOrigin = u_Lights[k].origin.xyz;
			lightColor = u_Lights[k].color;
			lightRadius = u_Lights[k].radius;
		}
		float importance = PomLightImportance(lightOrigin - position, lightColor, lightRadius);
		// insert into the descending list of the maxLights + 1 strongest
		for (int j = 0; j < 5; j++)
		{
			if (j > maxLights)
				break;
			if (importance > top[j])
			{
				float moved = top[j];
				top[j] = importance;
				importance = moved;
			}
		}
	}
	return max(top[maxLights], 1e-8);
#endif
}

float PomLocalLightWeight(in float cut, in vec3 toLight, in vec3 lightColor, in float lightRadius)
{
	if (cut < 0.0)
		return 0.0;
	if (cut == 0.0)
		return 1.0;
	return smoothstep(cut, cut * 1.5, PomLightImportance(toLight, lightColor, lightRadius));
}
#endif

#if defined(USE_DSHADOWS)
// r_shadowDebug 10: lowest cube shadow visibility of the lights reaching this
// fragment
float g_dlightShadowVisibility = 1.0;
#endif

// shadowLayer: cube index in u_ShadowMap2 (6 layers each), < 0 = unshadowed
vec3 EvaluateDynamicLight(
	in DLightSurface s,
	in vec3 lightOrigin,
	in vec3 lightColor,
	in float lightRadius,
	in int shadowLayer)
{
	vec3  L  = lightOrigin - s.position;
	float sqrLightDist = dot(L, L);

	float attenuation = CalcLightAttenuation(lightRadius * lightRadius / sqrLightDist);

	#if defined(USE_DSHADOWS)
		vec3 sampleVector = L;
		L /= sqrt(sqrLightDist);
		if (shadowLayer >= 0)
		{
			float dlightShadow;
			if (u_ShadowDebug.y > 0.5)
			{
				// r_dlightShadowBias 1: bias in cube texels at the receiver
				// (a 90 degree face spans 2 * distance): a normal offset
				// growing towards grazing angles plus a clamped slope term
				// covering the PCF footprint (about 2 texels)
				vec3 geoNormal = normalize(s.vertexNormal);
				float texelWorld = 2.0 * sqrt(sqrLightDist) / float(DSHADOW_MAP_SIZE);
				float cosTheta = clamp(dot(geoNormal, L), 0.0, 1.0);
				float slope = min(sqrt(1.0 - cosTheta * cosTheta) / max(cosTheta, 1e-3), 4.0);
				sampleVector -= geoNormal * (texelWorld * 1.5 * (1.0 - cosTheta));
				vec3 lookupDir = normalize(sampleVector);
				sampleVector -= lookupDir * (texelWorld * (1.0 + 2.0 * slope));
				float distance = getLightDepth(sampleVector, lightRadius);
				dlightShadow = pcfShadow(u_ShadowMap2, lookupDir, distance, shadowLayer);
			}
			else
			{
				sampleVector += L * tan(acos(dot(s.vertexNormal, -L)));
				float distance = getLightDepth(sampleVector, lightRadius);
				dlightShadow = pcfShadow(u_ShadowMap2, L, distance, shadowLayer);
			}
			if (attenuation > 0.0 && dot(s.N, L) > 0.0)
				g_dlightShadowVisibility = min(g_dlightShadowVisibility, dlightShadow);
			attenuation *= dlightShadow;
		}
	#else
		L /= sqrt(sqrLightDist);
	#endif
	attenuation *= DynamicLightReceiverVisibility(s, L);

	float NL = clamp(dot(s.N, L), 0.0, 1.0);
	#if defined(USE_SPECULARMAP)
	vec3  H  = normalize(L + s.E);
	float LH = clamp(dot(L, H), 0.0, 1.0);
	#elif !defined(USE_CLOTH_BRDF)
	float LH = 0.0;
	if (u_DiffuseBRDF == 1)
	{
		vec3 H = normalize(L + s.E);
		LH = clamp(dot(L, H), 0.0, 1.0);
	}
	#endif
	#if !defined(USE_CLOTH_BRDF)
	vec3 reflectance = M_PI * CalcDiffuse(s.diffuse, s.NE, NL, LH, s.roughness);
	#else
	vec3 reflectance = s.diffuse;
	#endif
	#if defined(USE_SSGI)
	// the diffuse lobe only (view independent), after shadows and receiver
	// visibility: the source of the screen-space GI
	g_ssgiDynamicDiffuse += lightColor * reflectance * attenuation * NL;
	#endif
	#if defined(USE_SKIN_SSS)
	// skin: wrapped diffuse lobe (r_skinSSS 1), kept apart for the diffusion
	vec3 lit = lightColor * reflectance * attenuation * SkinDiffuseNL(dot(s.N, L), NL) +
		SkinTransmission(s.N, s.E, L, lightColor * attenuation, s.diffuse);
	g_skinDiffuse += lit;
	  #if defined(USE_SPECULARMAP)
	float NH = clamp(dot(s.N, H), 0.0, 1.0);
	float VH = clamp(dot(s.E, H), 0.0, 1.0);
	lit += lightColor * CalcSpecular(s.specular, NH, NL, s.NE, LH, VH, s.roughness) * attenuation * NL;
	  #endif
	return lit;
	#else
	#if defined(USE_SPECULARMAP)
	float NH = clamp(dot(s.N, H), 0.0, 1.0);
	float VH = clamp(dot(s.E, H), 0.0, 1.0);
	reflectance += CalcSpecular(s.specular, NH, NL, s.NE, LH, VH, s.roughness);
	#endif
	return lightColor * reflectance * attenuation * NL;
	#endif
}

#if defined(USE_LTC)
/*
LTC area lights (tr_arealights.cpp), Forward+ only. Rectangles, and lines
(sabers) as a thin rectangle turned towards the receiver. The polygon integral
[Heitz et al. 2016] with the horizon clipped sphere approximation [Hill and
Heitz 2016]; tables from tools/ltcfit (tr_ltc_data.h):
  specular = FF(M^-1 * quad) * (F0 * norm + (1 - F0) * fresnel)
  diffuse  = FF(quad) * albedo           (exact Lambert form factor)
FF = form factor (cosine weighted solid angle / pi). The light color is the
emitted radiance. Attenuation is the geometry itself; the smooth window at the
influence range only hides the Forward+ cull radius.
*/
#define LTC_LUT_SIZE  64.0
#define LTC_LUT_SCALE ((LTC_LUT_SIZE - 1.0) / LTC_LUT_SIZE)
#define LTC_LUT_BIAS  (0.5 / LTC_LUT_SIZE)

#if defined(USE_LTC_DEBUG)
vec3 g_ltcSpecular = vec3(0.0);
vec3 g_ltcDiffuse = vec3(0.0);
vec3 g_ltcMode = vec3(0.0);		// source mode tint, weighted by contribution
float g_ltcBest = 0.0;
int g_ltcBestLight = -1;		// strongest area light here (r_ltcDebug 8)
#endif

// integral of the cosine lobe over one edge; the rational fit of
// theta / sin(theta) includes the 1 / (2 pi) of the form factor
vec3 LtcIntegrateEdgeVec(in vec3 v1, in vec3 v2)
{
	float x = dot(v1, v2);
	float y = abs(x);
	float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
	float b = 3.4175940 + (4.1616724 + y) * y;
	float v = a / b;
	float thetaSinTheta = (x > 0.0) ? v : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
	return cross(v1, v2) * thetaSinTheta;
}

// form factor of the quad q0..q3 (receiver at the origin, tangent frame,
// winding: cross(q1 - q0, q3 - q0) points away from the emitting side)
// transformed by Minv, clipped by the horizon
float LtcQuadFormFactor(in mat3 Minv, in vec3 q0, in vec3 q1, in vec3 q2, in vec3 q3, in bool twoSided)
{
	vec3 L0 = normalize(Minv * q0);
	vec3 L1 = normalize(Minv * q1);
	vec3 L2 = normalize(Minv * q2);
	vec3 L3 = normalize(Minv * q3);
	vec3 F = LtcIntegrateEdgeVec(L0, L1) + LtcIntegrateEdgeVec(L1, L2) +
		LtcIntegrateEdgeVec(L2, L3) + LtcIntegrateEdgeVec(L3, L0);
	float len = length(F);
	if (len <= 1e-7)
		return 0.0;
	float z = F.z / len;
	if (dot(q0, cross(q1 - q0, q3 - q0)) < 0.0)
	{
		// the back of the emitter
		if (!twoSided)
			return 0.0;
		z = -z;
	}
	vec2 uv = vec2(z * 0.5 + 0.5, len) * LTC_LUT_SCALE + LTC_LUT_BIAS;
	return len * texture(u_LtcAmplitudeMap, uv).w;
}

vec3 EvaluateAreaLight(in DLightSurface s, in FPlusLight light, in int lightIndex)
{
	int base = u_FPlusGrid.y + lightIndex * FPLUS_LIGHT_TEXELS;
	vec3 right = texelFetch(u_FPlusLights, base + 3).xyz;
	vec3 up = texelFetch(u_FPlusLights, base + 4).xyz;
	bool twoSided = (light.flags & AREALIGHT_TWO_SIDED) != 0;
	vec3 toReceiver = s.position - light.origin;

	if (light.type == FPLUS_TYPE_LINE)
	{
		// the blade seen from the receiver: a ribbon one tube diameter wide,
		// facing it (same projected area as the tube)
		vec3 n = toReceiver - right * dot(toReceiver, right);
		float l = length(n);
		if (l < 1e-3)
			return vec3(0.0);
		up = cross(n / l, right);
		twoSided = true;
	}
	else if (!twoSided && dot(toReceiver, cross(right, up)) <= 0.0)
		return vec3(0.0);

	// influence window from the closest point of the emitter
	vec3 closest = light.origin +
		right * clamp(dot(toReceiver, right), -light.halfWidth, light.halfWidth) +
		up * clamp(dot(toReceiver, up), -light.halfHeight, light.halfHeight);
	float d = length(s.position - closest) / max(light.radius, 1.0);
	float d2 = d * d;
	float window = clamp(1.0 - d2 * d2, 0.0, 1.0);
	window *= window;
	if (window <= 0.0)
		return vec3(0.0);

	// POM self shadow: towards the centre (one ray, not one per corner)
	window *= DynamicLightReceiverVisibility(s, normalize(light.origin - s.position));

	// receiver tangent frame, T1 in the plane of N and E
	vec3 N = s.N;
	float NE = dot(N, s.E);
	vec3 T1 = s.E - N * NE;
	if (dot(T1, T1) < 1e-8)
		T1 = abs(N.z) < 0.999 ? cross(N, vec3(0.0, 0.0, 1.0)) : vec3(1.0, 0.0, 0.0);
	T1 = normalize(T1);
	vec3 T2 = cross(N, T1);
	mat3 toTangent = transpose(mat3(T1, T2, N));

	vec3 R = right * light.halfWidth;
	vec3 U = up * light.halfHeight;
	vec3 c = light.origin - s.position;
	vec3 q0 = toTangent * (c - R - U);
	vec3 q1 = toTangent * (c - R + U);
	vec3 q2 = toTangent * (c + R + U);
	vec3 q3 = toTangent * (c + R - U);

	vec3 radiance = light.color * window;
	vec3 diffuseOut = vec3(0.0);
	vec3 specularOut = vec3(0.0);
	float formFactor = 0.0;

	// static stock lamps: the lightmap already has their diffuse light
	#if defined(USE_CLOTH_BRDF)
	formFactor = LtcQuadFormFactor(mat3(1.0), q0, q1, q2, q3, twoSided);
	#endif
	if ((light.flags & AREALIGHT_SPECULAR_ONLY) == 0)
	{
		#if !defined(USE_CLOTH_BRDF)
		formFactor = LtcQuadFormFactor(mat3(1.0), q0, q1, q2, q3, twoSided);
		#endif
		diffuseOut = radiance * s.diffuse * formFactor;
		#if defined(USE_SKIN_SSS)
		g_skinDiffuse += diffuseOut;
		#endif
		#if defined(USE_SSGI)
		// view independent diffuse only: the screen-space GI source
		g_ssgiDynamicDiffuse += diffuseOut;
		#endif
	}

	#if defined(USE_SPECULARMAP)
	#if !defined(USE_CLOTH_BRDF)
	vec2 uv = vec2(sqrt(clamp(s.roughness, 0.0, 1.0)), sqrt(1.0 - clamp(NE, 0.0, 1.0)));
	uv = uv * LTC_LUT_SCALE + LTC_LUT_BIAS;
	vec4 t1 = texture(u_LtcMatrixMap, uv);
	vec4 t2 = texture(u_LtcAmplitudeMap, uv);
	mat3 Minv = mat3(vec3(t1.x, 0.0, t1.y), vec3(0.0, 1.0, 0.0), vec3(t1.z, 0.0, t1.w));
	float specFF = LtcQuadFormFactor(Minv, q0, q1, q2, q3, twoSided);
	// Schlick split as F_Schlick, including its no-specular cut
	vec3 F = s.specular * t2.x + (1.0 - s.specular) * t2.y * clamp(50.0 * s.specular.g, 0.0, 1.0);
	specularOut = radiance * specFF * F;
	#else
	// cloth (Charlie) lobe: wide, a representative point is enough
	vec3 L = normalize(closest - s.position);
	vec3 H = normalize(L + s.E);
	float NL = clamp(dot(N, L), 0.0, 1.0);
	specularOut = radiance * M_PI * formFactor * CalcSpecular(s.specular,
		clamp(dot(N, H), 0.0, 1.0), NL, NE, clamp(dot(L, H), 0.0, 1.0),
		clamp(dot(s.E, H), 0.0, 1.0), s.roughness);
	#endif
	#endif

	#if defined(USE_LTC_DEBUG)
	g_ltcSpecular += specularOut;
	g_ltcDiffuse += diffuseOut;
	vec3 tint = light.type == FPLUS_TYPE_LINE ? vec3(0.1, 1.0, 0.2) :
		(light.flags & AREALIGHT_DYNAMIC) != 0 ? vec3(1.0, 0.5, 0.05) :
		(light.flags & AREALIGHT_SPECULAR_ONLY) != 0 ? vec3(0.1, 0.35, 1.0) : vec3(0.1, 0.9, 1.0);
	float strength = dot(specularOut + diffuseOut, vec3(0.2126, 0.7152, 0.0722));
	g_ltcMode += tint * strength;
	if (strength > g_ltcBest)
	{
		g_ltcBest = strength;
		g_ltcBestLight = lightIndex;
	}
	#endif
	return diffuseOut + specularOut;
}
#endif

vec3 CalcDynamicLightContribution(
	in float roughness,
	in vec3 N,
	in vec3 E,
	in vec3 viewOrigin,
	in vec3 viewDir,
	in float NE,
	in vec3 diffuse,
	in vec3 specular,
	in vec3 vertexNormal)
{
	vec3 outColor = vec3(0.0);

	DLightSurface s;
	s.position = viewOrigin - viewDir;
	s.N = N;
	s.E = E;
	s.NE = NE;
	s.diffuse = diffuse;
	s.specular = specular;
	s.roughness = roughness;
	s.vertexNormal = vertexNormal;

	if (u_LightMask == 0)
		return outColor;

	// one loop and one EvaluateDynamicLight call site for both paths: every
	// lightall permutation inlines it, a second copy doubled the compile time
	bool fplus = FPlusEnabled();
	ivec2 list = fplus ? FPlusClusterLights(s.position) : ivec2(0, min(u_NumLights, MAX_DLIGHTS));
#if defined(USE_PARALLAXMAP)
	float pomCut = PomLocalLightCut(s.position, fplus, list);
#endif
	for (int k = 0; k < list.y; k++)
	{
		vec3 lightOrigin, lightColor;
		float lightRadius;
		int shadowLayer;
		if (fplus)
		{
			int lightIndex = FPlusLightIndex(list.x + k);
			FPlusLight light = FPlusFetchLight(lightIndex);
			if (FPlusDebugSkipLight(light, lightIndex))
				continue;
			if (light.type != 0.0)
			{
				// area light (r_ltcAreaLights): only in the USE_LTC programs
#if defined(USE_LTC)
#if defined(USE_PARALLAXMAP)
				g_pomLightWeight = PomLocalLightWeight(pomCut, light.origin - s.position, light.color, light.radius);
#endif
				outColor += EvaluateAreaLight(s, light, lightIndex);
#endif
				continue;
			}
			lightOrigin = light.origin;
			lightColor = light.color;
			lightRadius = light.radius;
			shadowLayer = light.shadowSlot;
		}
		else
		{
			if ( ( u_LightMask & ( 1 << k ) ) == 0 )
				continue;
			lightOrigin = u_Lights[k].origin.xyz;
			lightColor = u_Lights[k].color;
			lightRadius = u_Lights[k].radius;
			shadowLayer = k;
		}
#if defined(USE_PARALLAXMAP)
		g_pomLightWeight = PomLocalLightWeight(pomCut, lightOrigin - s.position, lightColor, lightRadius);
#endif
		outColor += EvaluateDynamicLight(s, lightOrigin, lightColor, lightRadius, shadowLayer);
	}
	return outColor;
}
#else
vec3 EvaluateDynamicLightSimple(
	in vec3 position,
	in vec3 N,
	in vec3 lightOrigin,
	in vec3 lightColor,
	in float lightRadius)
{
	vec3 L = lightOrigin - position;
	float sqrLightDist = dot(L, L);
	float attenuation = CalcLightAttenuation(lightRadius * lightRadius / sqrLightDist);
	L /= sqrt(sqrLightDist);
	float NL = clamp(dot(N, L), 0.0, 1.0);
	return lightColor * attenuation * NL;
}

vec3 CalcDynamicLightContribution(
	in vec3 position,
	in vec3 N )
{
	vec3 outLight = vec3(0.0);
	if (u_LightMask == 0)
		return outLight;

	bool fplus = FPlusEnabled();
	ivec2 list = fplus ? FPlusClusterLights(position) : ivec2(0, min(u_NumLights, MAX_DLIGHTS));
	for (int k = 0; k < list.y; k++)
	{
		vec3 lightOrigin, lightColor;
		float lightRadius;
		if (fplus)
		{
			int lightIndex = FPlusLightIndex(list.x + k);
			FPlusLight light = FPlusFetchLight(lightIndex);
			if (light.type != 0.0 || FPlusDebugSkipLight(light, lightIndex))
				continue;
			lightOrigin = light.origin;
			lightColor = light.color;
			lightRadius = light.radius;
		}
		else
		{
			if ( ( u_LightMask & ( 1 << k ) ) == 0 )
				continue;
			lightOrigin = u_Lights[k].origin.xyz;
			lightColor = u_Lights[k].color;
			lightRadius = u_Lights[k].radius;
		}
		outLight += EvaluateDynamicLightSimple(position, N, lightOrigin, lightColor, lightRadius);
	}
	return outLight;
}
#endif

// r_forwardPlusDebug views that replace the lit color (1-5, 8); 6, 7 and 9
// show the filtered dynamic light alone. False = not a debug view here.
vec3 FPlusHashColor(in int n)
{
	return fract(sin(vec3(float(n)) * vec3(12.9898, 78.233, 37.719)) * 43758.5453) * 0.8 + 0.2;
}

bool FPlusDebugColor(in vec3 position, in vec3 litColor, in vec3 dynamicLight, out vec3 color)
{
	color = litColor;
#if !defined(USE_FPLUS_DEBUG)
	return false;
#else
	int mode = int(u_FPlusDebug.x);
	if (!FPlusEnabled() || mode <= 0)
		return false;

	if (mode == 6 || mode == 7 || mode == 9)
	{
		color = dynamicLight;
		return true;
	}

	ivec2 list = FPlusClusterLights(position);
	float maxLights = max(u_FPlusDebug.z, 1.0);
	if (mode == 1)
	{
		vec2 p = mod(gl_FragCoord.xy - u_FPlusParams2.xy, u_FPlusParams.x);
		bool edge = p.x < 1.0 || p.y < 1.0;
		color = edge ? vec3(1.0, 0.85, 0.1) : litColor;
	}
	else if (mode == 2)
		color = FPlusHashColor(FPlusSlice(position) + 7) * (0.35 + 0.65 * clamp(dot(litColor, vec3(0.333)), 0.0, 1.0));
	else if (mode == 3)
		color = FPlusHashColor(FPlusCluster(position));
	else if (mode == 4)
	{
		// black = none, blue -> green -> red = up to the per cluster limit
		float t = float(list.y) / maxLights;
		color = list.y == 0 ? vec3(0.02) :
			(t < 0.5 ? mix(vec3(0.0, 0.1, 1.0), vec3(0.0, 1.0, 0.1), t * 2.0) :
				mix(vec3(0.0, 1.0, 0.1), vec3(1.0, 0.05, 0.0), t * 2.0 - 1.0));
	}
	else if (mode == 5)
		color = float(list.y) >= maxLights ? vec3(1.0, 0.0, 0.0) : litColor * 0.25;
	else if (mode == 8)
	{
		// lights whose sphere contains the point, tinted by index, brighter at the centre
		vec3 sum = vec3(0.0);
		for (int k = 0; k < list.y; k++)
		{
			int lightIndex = FPlusLightIndex(list.x + k);
			FPlusLight light = FPlusFetchLight(lightIndex);
			float d = length(light.origin - position) / max(light.radius, 1.0);
			if (d < 1.0)
				sum += FPlusHashColor(lightIndex) * (0.25 + 0.75 * (1.0 - d)) * 0.5;
		}
		color = litColor * 0.15 + sum;
	}
	return true;
#endif
}

#if defined(USE_LTC_DEBUG) && defined(PER_PIXEL_LIGHTING)
// r_ltcDebug (tr_arealights.cpp): 1 specular, 2 diffuse, 3 source mode,
// 4 area lights per cluster, 5 influence bounds, 8 strongest light id.
// 6 / 7 (outlines / normals) are polygons drawn on the lit image.
bool LtcDebugColor(in vec3 position, in vec3 litColor, out vec3 color)
{
	color = litColor;
	int mode = int(u_FPlusDebug.w);
	if (!FPlusEnabled() || mode <= 0 || mode == 6 || mode == 7 || mode > 8)
		return false;

	if (mode == 1)
		color = g_ltcSpecular;
	else if (mode == 2)
		color = g_ltcDiffuse;
	else if (mode == 3)
		color = g_ltcMode + litColor * 0.05;
	else if (mode == 4 || mode == 5)
	{
		ivec2 list = FPlusClusterLights(position);
		int count = 0;
		vec3 sum = vec3(0.0);
		for (int k = 0; k < list.y; k++)
		{
			int lightIndex = FPlusLightIndex(list.x + k);
			FPlusLight light = FPlusFetchLight(lightIndex);
			if (light.type == 0.0)
				continue;
			count++;
			float d = length(light.origin - position) / max(light.radius, 1.0);
			if (d < 1.0)
			{
				vec3 c = (light.flags & AREALIGHT_SELECTED) != 0 ? vec3(1.0) : FPlusHashColor(lightIndex);
				sum += c * (0.25 + 0.75 * (1.0 - d)) * 0.5;
			}
		}
		if (mode == 4)
			color = count == 0 ? litColor * 0.15 :
				mix(vec3(0.0, 0.2, 1.0), vec3(1.0, 0.1, 0.0), clamp(float(count - 1) / 7.0, 0.0, 1.0));
		else
			color = litColor * 0.15 + sum;
	}
	else if (mode == 8)
		color = g_ltcBestLight < 0 ? litColor * 0.1 : FPlusHashColor(g_ltcBestLight);
	return true;
}
#endif

float luma(vec3 color)
{
	const vec3 weight = vec3(0.2126, 0.7152, 0.0722);
	return dot(color, weight);
}

vec3 CalcIBLContribution(
	in float roughness,
	in vec3 N,
	in vec3 E,
	in vec3 viewOrigin,
	in vec3 viewDir,
	in float NE,
	in vec3 specular,
	in vec3 lighting
)
{
#if defined(PER_PIXEL_LIGHTING) && defined(USE_CUBEMAP) && defined(USE_SPECULARMAP)
	// parallax corrected cubemap (cheaper trick)
	// from http://seblagarde.wordpress.com/2012/09/29/image-based-lighting-approaches-and-parallax-corrected-cubemap/
	vec3 parallax = u_CubeMapInfo.xyz + u_CubeMapInfo.w * viewDir;

	vec3 R = reflect(-E, N) - parallax;
	vec4 cubeLightColor = textureLod(u_CubeMap, R, roughness * ROUGHNESS_MIPS) * u_EnableTextures.w;

	// Scale reflection based on current light luminance / max luminance of the cubemap 
	cubeLightColor.rgb *= clamp(luma(lighting) / cubeLightColor.a, 0.0, 1.0);

	// Base BRDF
	#if !defined(USE_CLOTH_BRDF)
		vec2 EnvBRDF = texture(u_EnvBrdfMap, vec2(roughness, NE)).rg;
		return cubeLightColor.rgb * (specular.rgb * EnvBRDF.x + EnvBRDF.y);
	// Cloth BRDF
	#else
		float EnvBRDF = texture(u_EnvBrdfMap, vec2(roughness, NE)).b;
		return cubeLightColor.rgb * EnvBRDF;
	#endif
#else
	return vec3(0.0);
#endif
}

#if defined(USE_SSR)
// The factor CalcIBLContribution applies to the cubemap radiance: SSR
// replaces cubemap radiance with screen-space radiance under the same BRDF.
vec3 SSRSpecularWeight(in float roughness, in float NE, in vec3 specular)
{
#if defined(PER_PIXEL_LIGHTING) && defined(USE_SPECULARMAP)
	#if !defined(USE_CLOTH_BRDF)
		vec2 EnvBRDF = texture(u_EnvBrdfMap, vec2(roughness, NE)).rg;
		return specular.rgb * EnvBRDF.x + EnvBRDF.y;
	#else
		return vec3(texture(u_EnvBrdfMap, vec2(roughness, NE)).b);
	#endif
#else
	return vec3(0.0);
#endif
}
#endif

#if defined(USE_WETNESS) && defined(PER_PIXEL_LIGHTING)
// 0..1 rain exposure of a world position: the particle test of weather.glsl
// (culled when depth > stored depth) against the same map, with a small depth
// bias and a bilinear blend of 4 binary tests for a soft, stable 1 texel edge.
float ComputeRainExposure(in vec3 worldPosition, in vec3 geometricNormal, in float normalOffset)
{
	// half a texel along the normal (normalOffset): walls test the column in
	// front of them instead of their own top
	vec4 p = u_WeatherMvp * vec4(worldPosition + geometricNormal * normalOffset, 1.0);
	vec3 uvz = p.xyz / p.w * 0.5 + 0.5;
	if (any(lessThan(uvz.xy, vec2(0.0))) || any(greaterThan(uvz.xy, vec2(1.0))))
		return 0.0;

	// slope scaled: steep faces vary more in depth across one texel
	float bias = u_WetnessParams2.x * (1.0 + 2.0 * (1.0 - abs(geometricNormal.z)));
	float z = uvz.z - bias;

	ivec2 size = textureSize(u_WeatherDepthMap, 0);
	vec2 texel = uvz.xy * vec2(size) - 0.5;
	ivec2 base = ivec2(floor(texel));
	vec2 f = texel - vec2(base);
	ivec2 maxTexel = size - 1;
	float e00 = step(z, texelFetch(u_WeatherDepthMap, clamp(base,               ivec2(0), maxTexel), 0).r);
	float e10 = step(z, texelFetch(u_WeatherDepthMap, clamp(base + ivec2(1, 0), ivec2(0), maxTexel), 0).r);
	float e01 = step(z, texelFetch(u_WeatherDepthMap, clamp(base + ivec2(0, 1), ivec2(0), maxTexel), 0).r);
	float e11 = step(z, texelFetch(u_WeatherDepthMap, clamp(base + ivec2(1, 1), ivec2(0), maxTexel), 0).r);
	return mix(mix(e00, e10, f.x), mix(e01, e11, f.x), f.y);
}

// Procedural puddles: world anchored low frequency value noise, one domain
// warp and two octaves (3 noise evaluations, 12 hashes), 0..1.
float PuddleHash(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float PuddleValueNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = p - i;
	vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	float a = PuddleHash(i);
	float b = PuddleHash(i + vec2(1.0, 0.0));
	float c = PuddleHash(i + vec2(0.0, 1.0));
	float d = PuddleHash(i + vec2(1.0, 1.0));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float PuddleField(vec2 p)
{
	float w = PuddleValueNoise(p * 0.5 + 17.3);
	vec2 q = p + (w - 0.5) * 0.8;
	return 0.65 * PuddleValueNoise(q) + 0.35 * PuddleValueNoise(q * 2.3 + 5.1);
}

// Water runoff (r_weatherRunoff): a thin film with streams running down
// slopes and walls. On any plane the downhill direction (gravity projected
// on the surface) is perpendicular to the contour lines, which are
// horizontal, so a field of (horizontal across coordinate, world z) that is
// stretched along z streams along projected gravity on every slope, and a
// pattern that moves toward lower z can never run uphill. World anchored:
// no swimming with the camera, no UV seams.

// Value noise whose lattice repeats every 256 cells along y (the flow
// axis): the CPU flow clock wraps at 256 cells without a jump.
float RunoffNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = p - i;
	vec2 u = f * f * (3.0 - 2.0 * f);
	float y0 = mod(i.y, 256.0);
	float y1 = mod(i.y + 1.0, 256.0);
	float a = PuddleHash(vec2(i.x, y0));
	float b = PuddleHash(vec2(i.x + 1.0, y0));
	float c = PuddleHash(vec2(i.x, y1));
	float d = PuddleHash(vec2(i.x + 1.0, y1));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// across, along (world z): pattern cells, clock: cells (u_RunoffParams.z).
// Static channels (water keeps its paths) of two widths, wiggled by a low
// frequency warp, and pulses running down them at two speeds (1 and 1 / 1.7
// x r_runoffSpeed), so the whole does not read as one scrolling texture.
// Returns x = film (thin sheet everywhere, full in the streams, pulsing),
// y = stream core.
vec2 RunoffStreaks(float across, float along, float clock)
{
	float w = PuddleValueNoise(vec2(across * 0.35 + 3.1, along * 0.15));
	float a = across + (w - 0.5) * 1.6;
	float wide = smoothstep(0.52, 0.78, PuddleValueNoise(vec2(a * 1.3, along * 0.09 + 7.7)));
	float narrow = smoothstep(0.60, 0.80, PuddleValueNoise(vec2(a * 3.7 + 11.0, along * 0.22 + 2.9)));
	float stream = max(wide, 0.8 * narrow);
	// the along lattice of the moving layers repeats every 256 cells and the
	// clock enters with an integer factor: seamless clock wrap
	float p1 = RunoffNoise(vec2(a * 3.1 + 5.0, along + clock));
	float p2 = RunoffNoise(vec2(a * 5.3 + 19.0, along * 1.7 + clock));
	float pulse = 0.55 + 0.45 * smoothstep(0.25, 0.85, 0.6 * p1 + 0.4 * p2);
	return vec2(mix(0.25, 1.0, stream) * pulse, stream * pulse);
}

// One streak field for the bin k (0..7) of the contour direction: across
// runs along the fixed horizontal axis at k x 22.5 degrees in the pattern
// frame. A drop drifting with the wind keeps across + shear x z constant.
vec2 RunoffBin(in vec3 rel, in float k)
{
	float angle = k * (M_PI / 8.0);
	vec2 axis = u_RunoffFrame.xy * cos(angle) + vec2(-u_RunoffFrame.y, u_RunoffFrame.x) * sin(angle);
	float across = (dot(rel.xy, axis) + dot(u_RunoffParams2.xy, axis) * rel.z) * u_RunoffParams.y;
	return RunoffStreaks(across + k * 37.0, rel.z * u_RunoffParams.y, u_RunoffParams.z);
}

// The across coordinate has to follow the contour line (horizontal, in the
// surface), but rotating it per pixel distorts badly at large world
// coordinates on curved rock. So the contour direction picks the nearest of
// 8 fixed axes (world axes, or an entity's yaw frame): walls are exact with
// any horizontal axis (flow is straight down), slopes are off by at most
// 11 degrees. One evaluation for 70 % of the directions, a continuous blend
// of two bins in between.
vec2 RunoffPattern(in vec3 worldPosition, in vec3 geometricNormal)
{
	vec2 a1 = u_RunoffFrame.xy;
	vec2 a2 = vec2(-a1.y, a1.x);
	vec3 rel = worldPosition - vec3(u_RunoffFrame.zw, u_RunoffParams2.w);
	// contour = perp(normal) in the frame, its angle mod 180 degrees in bins
	float n1 = dot(geometricNormal.xy, a1);
	float n2 = dot(geometricNormal.xy, a2);
	float bin = mod(atan(n1, -n2) * (8.0 / M_PI), 8.0);
	float k0 = floor(bin);
	float blend = clamp((bin - k0 - 0.35) / 0.3, 0.0, 1.0);
	vec2 result = vec2(0.0);
	if (blend < 1.0)
		result += (1.0 - blend) * RunoffBin(rel, k0);
	if (blend > 0.0)
		result += blend * RunoffBin(rel, mod(k0 + 1.0, 8.0));
	return result;
}

// Rain ripples on standing water (r_puddleRipples): expanding rings that only
// tilt the water normal, so direct light, IBL, SSR and SSGI all show them.
// 3 hashes of the PuddleHash family, 0..1.
vec3 RippleHash3(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yxz + 33.33);
	return fract((p3.xxy + p3.yzz) * p3.zyx);
}

// One expanding ring. p, center, maxRadius and width share one unit (world
// or cell); phase 0..1 is its life. The wave packet x (1 - x^2)^2 (leading
// crest, trailing trough, no trig) is 2 x width wide around the front at
// phase x maxRadius and fades while the ring grows. Returns (height, dh/dp),
// the height scaled so the slope peaks at amp in any unit. Future splash
// impacts (world center, spawn time) can call this next to the procedural
// rings.
vec3 RippleRing(vec2 p, vec2 center, float phase, float amp, float maxRadius, float width)
{
	vec2 v = p - center;
	float invD = inversesqrt(max(dot(v, v), 1e-8));
	float x = (dot(v, v) * invD - phase * maxRadius) / width;
	float w = max(1.0 - x * x, 0.0);
	float a = amp * min(phase * 16.0, 1.0) * (1.0 - phase) * (1.0 - phase);
	float height = a * width * x * w * w;
	float slope = a * w * (1.0 - 5.0 * x * x);	// d height / d distance
	return vec3(height, v * (slope * invD));
}

// One jittered, rotated cell grid in world XY. Every cell hosts one ring at a
// time that stays inside it (center jitter 0.18 + radius 0.3 + half packet
// 0.07: the last 17 % of its life, at < 3 % amplitude, may touch the edge), so
// one ring per layer is enough. Each ring cycle takes the next point of a per
// cell R2 sequence: a new center, strength and whether the cell rings at all
// (density). The small jitter box would make rings start from the same spots
// in a long static shot, so the whole grid moves every 4 clock cycles (an
// epoch); a ring that would live across an epoch change is skipped (1 of 4
// per cell), so no ring is ever cut. A layer is empty at its epoch change,
// so the layers' clocks are staggered (clockOffset) to keep the rain steady.
// Returns (height, dh/dxy) in world units.
vec3 RippleLayer(vec2 worldXY, mat2 rot, float scale, float seed, float clockOffset, float footprint)
{
	const float radius = 0.3;
	const float width = 0.07;
	float toCell = u_PuddleRipple.y * scale;
	float clock = u_PuddleRipple.z + clockOffset;
	float epoch = floor(clock * 0.25);
	// mod 64: the clock wraps at 256 cycles = 64 epochs
	vec2 q = rot * (worldXY * toCell) + seed + fract(mod(epoch, 64.0) * vec2(0.7548777, 0.5698403));
	vec2 cell = floor(q);
	vec3 h = RippleHash3(cell + seed);
	float cycle = clock + h.z;
	float ringIndex = mod(floor(cycle), 256.0);	// same wrap as the clock
	vec3 r = fract(h + ringIndex * vec3(0.7548777, 0.5698403, 0.6180340));
	float birth = floor(cycle) - h.z;	// clock time the ring started
	float amp = step(r.z, u_PuddleRipple.w) * (0.6 + 0.4 * r.x) *
		step(4.0 * epoch, birth) * step(birth, 4.0 * epoch + 3.0);
	// rings narrower than about a pixel (2 x width) would alias: fade them
	// out to the flat surface
	amp *= clamp(1.43 - footprint * toCell * (0.7 / width), 0.0, 1.0);
	vec3 ring = RippleRing(q, cell + 0.5 + (r.xy - 0.5) * 0.36, fract(cycle), amp, radius, width);
	// cell -> world: height / toCell, gradient (row vector) x rot
	return vec3(ring.x / toCell, ring.yz * rot);
}

// Procedural rings: 3 layers at different angles and scales hide the grids.
// Real impact events could later add their rings here or replace these.
vec3 PuddleRipples(vec2 worldXY, float footprint)
{
	vec3 sum = RippleLayer(worldXY, mat2(1.0, 0.0, 0.0, 1.0), 1.0, 0.0, 0.0, footprint);
	sum += RippleLayer(worldXY, mat2(0.7986, 0.6018, -0.6018, 0.7986), 0.79, 3.3, 4.0 / 3.0, footprint);
	sum += RippleLayer(worldXY, mat2(0.3256, -0.9455, 0.9455, 0.3256), 1.27, 7.7, 8.0 / 3.0, footprint);
	return sum;
}

#if defined(USE_PARALLAXMAP)
// Height aware puddles (r_puddleHeight): the macro basin (0 where the macro
// puddle fringe starts, 1 in its core) sets a static water level inside the
// relief of the material, depth = PuddleRelief (1 = deepest). The
// deepest cracks fill first, then the low areas, the core covers the peaks.
// x = shallow film (bumps still show), y = submerged, z = normal flattening.
vec3 PuddleMicro(in float basin, in float depth)
{
	float soft = u_PuddleHeight.z;
	float fill = basin * (1.0 + 2.0 * soft + u_PuddleHeight.w) - soft;
	float level = 1.0 - fill;
	float edge = smoothstep(level - soft, level, depth);
	float core = smoothstep(level, level + soft, depth);
	return vec3(edge, core, max(smoothstep(level, level + 2.0 * soft, depth), 0.5 * edge));
}

// SampleMaterialDepth rescaled to the relief this height map really uses
// (2nd..98th percentile, image_t heightRange): 0 = its peaks, 1 = its deepest
float PuddleRelief(in float materialDepth)
{
	return clamp((materialDepth - u_PuddleHeight.x) * u_PuddleHeight.y, 0.0, 1.0);
}
#endif
#endif

#if defined(PER_PIXEL_LIGHTING) && defined(USE_SSAO)
// Jimenez et al. 2016, "Practical Real-Time Strategies for Accurate Indirect
// Occlusion": multi-bounce fit, bright albedo loses less light in creases
vec3 AOMultiBounce(float visibility, vec3 albedo)
{
	vec3 a =  2.0404 * albedo - 0.3324;
	vec3 b = -4.7951 * albedo + 0.6417;
	vec3 c =  2.7552 * albedo + 0.6903;
	return max(vec3(visibility), ((visibility * a + b) * visibility + c) * visibility);
}

// world space GTAO bent normal, octahedral in u_SSAOMap.ba (ao_composite.glsl)
vec3 AODecodeBentNormal(vec2 e)
{
	e = e * 2.0 - 1.0;
	vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
	float t = max(-n.z, 0.0);
	n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
	return normalize(n);
}

// Solid angle of the intersection of two spherical caps (half angle cosines
// cosC1, cosC2, cosine of the angle between their axes cosB), with the
// smoothstep approximation of Oat & Sander 2007 as used by Unity HDRP
float SphericalCapIntersection(float cosC1, float cosC2, float cosB)
{
	float r1 = acos(clamp(cosC1, -1.0, 1.0));
	float r2 = acos(clamp(cosC2, -1.0, 1.0));
	float rd = acos(clamp(cosB, -1.0, 1.0));
	float capArea = 2.0 * M_PI * (1.0 - max(cosC1, cosC2));
	if (rd <= abs(r1 - r2))
		return capArea; // one cap inside the other
	if (rd >= r1 + r2)
		return 0.0;
	float diff = abs(r1 - r2);
	float x = 1.0 - clamp((rd - diff) / max(r1 + r2 - diff, 1e-4), 0.0, 1.0);
	return smoothstep(0.0, 1.0, x) * capArea;
}

// Specular occlusion of environment (cubemap) reflections from AO, u_AOParams2.y:
//   0: scalar AO (specularIBL *= AO)
//   1: Lagarde & de Rousiers 2014, "Moving Frostbite to PBR":
//      saturate(pow(N.V + ao, exp2(-16 alpha - 1)) - 1 + ao), alpha = roughness^2
//   2: Jimenez et al. 2016 cone / cone: the visibility cone around the bent
//      normal B (cos = sqrt(1 - ao), cosine weighted AO) against the GGX lobe
//      cone around R (cos = 10^-alpha^2), relative to the lobe part above the
//      surface, so an unoccluded surface is exactly 1 at any angle and a
//      mirror stays lit while R points into the unoccluded cone
float AOSpecularOcclusion(vec3 N, vec3 E, vec3 B, float NE, float visibility, float roughness)
{
	int mode = int(u_AOParams2.y);
	if (mode == 0)
		return visibility;

	float alpha = roughness * roughness;
	if (mode == 1)
		return clamp(pow(NE + visibility, exp2(-16.0 * alpha - 1.0)) - 1.0 + visibility, 0.0, 1.0);

	alpha = max(alpha, 0.01);
	vec3 R = reflect(-E, N);
	float cosVisible = sqrt(clamp(1.0 - visibility, 0.0, 1.0));
	float cosLobe = exp2(-3.32193 * alpha * alpha);
	float visible = SphericalCapIntersection(cosVisible, cosLobe, dot(B, R));
	float aboveSurface = SphericalCapIntersection(0.0, cosLobe, dot(N, R));
	return aboveSurface > 1e-6 ? clamp(visible / aboveSurface, 0.0, 1.0) : visibility;
}
#endif

vec3 CalcNormal( in vec3 vertexNormal, in vec4 vertexTangent, in vec2 texCoords )
{
#if defined(USE_NORMALMAP)
	vec3 biTangent = vertexTangent.w * cross(vertexNormal, vertexTangent.xyz);
#if defined(USE_SILHOUETTE_POM)
	vec3 N = textureGrad(u_NormalMap, texCoords, g_pomGradX, g_pomGradY).agb - vec3(0.5);
#else
	vec3 N = texture(u_NormalMap, texCoords).agb - vec3(0.5);
#endif
	N.xy *= u_NormalScale.xy;
	N.z = sqrt(clamp((0.25 - N.x * N.x) - N.y * N.y, 0.0, 1.0));
	N = N.x * vertexTangent.xyz + N.y * biTangent + N.z * vertexNormal;
	return normalize(N);
#else
	return normalize(vertexNormal);
#endif
}

#if defined(USE_SILHOUETTE_POM)
// Silhouette POM (pom_silhouette.glsl): crossfade, ordinary POM for base
// surfaces inside the crossfade band, ray / height field intersection for
// shells. Returns the texture and lightmap coordinates and the view vector
// (camera - surface point) of the virtual surface, writes gl_FragDepth.
void PomSilhouetteFragment(inout vec2 texCoords, inout vec2 lmCoords, out vec3 viewDir,
	out PomHit hit, out bool shell)
{
	vec3 position = u_ViewOrigin - var_ViewDir.xyz;
	float viewDistance = length(var_ViewDir.xyz);
	vec2 uvDx = dFdx(texCoords);
	vec2 uvDy = dFdy(texCoords);
	shell = PomIsShellDraw();
	viewDir = var_ViewDir.xyz;
	hit.hit = false;
	hit.entryInside = false;
	hit.uv = texCoords;
	hit.lmUV = lmCoords;
	hit.position = position;
	hit.depth = 0.0;
	hit.t = 0.0;
	hit.samples = 0.0;

	bool keep = PomFadeKeep(position, u_ViewOrigin, gl_FragCoord.xy, shell);
	if (!shell)
	{
		vec3 tangentViewDir = vec3(var_LightDir.w, var_Normal.w, var_ViewDir.w);
		texCoords += GetParallaxOffset(texCoords, tangentViewDir);
		g_pomGradX = dFdx(texCoords);
		g_pomGradY = dFdy(texCoords);
		if (!keep)
			discard;
		gl_FragDepth = gl_FragCoord.z;
		return;
	}
	if (!keep)
		discard;

	vec3 N = normalize(var_Normal.xyz);
	vec3 T = normalize(var_Tangent.xyz - N * dot(N, var_Tangent.xyz));
	vec3 B = cross(N, T) * var_Tangent.w;
	vec3 rayDir = -var_ViewDir.xyz / viewDistance;
	float parallaxDepth = u_NormalScale.a;
	vec2 aspect = PomAspect(vec2(textureSize(u_NormalMap, 0)));
	float pixelFootprint = viewDistance * 2.0 * length(u_ViewUp) / (u_ViewInfo.y * r_FBufScale.y);
	PomGradients(PomIsWall(var_PomHeader), uvDx, uvDy, pixelFootprint, var_PomShell.y,
		parallaxDepth, aspect, g_pomGradX, g_pomGradY);

	hit = PomSilhouetteTrace(u_NormalMap, aspect, parallaxDepth, position, rayDir, texCoords,
		var_PomShell.x, var_PomShell.y, PomHeaderTexel(var_PomHeader), T, B, N, g_pomGradX, g_pomGradY);

	// the shell hit feeds the same self shadow rays as ordinary POM
	g_pom.valid = hit.hit;
	g_pom.uv = hit.uv;
	g_pom.depth = hit.depth;
	g_pom.T = T;
	g_pom.B = B;
	g_pom.N = N;
	g_pom.scale = aspect * parallaxDepth;
	g_pom.gradX = g_pomGradX;
	g_pom.gradY = g_pomGradY;
	g_pom.fade = 1.0;
	g_pom.viewSamples = hit.samples;
	g_pom.shadowSamples = 0.0;
	if (!hit.hit)
	{
		// r_pomSilhouetteDebug 6 keeps the pixels the ray missed
		if (int(u_PomParams2.w) != 6)
			discard;
		gl_FragDepth = gl_FragCoord.z;
		return;
	}

	texCoords = hit.uv;
  #if defined(USE_LIGHTMAP)
	lmCoords = hit.lmUV;
  #endif
	viewDir = u_ViewOrigin - hit.position;
	gl_FragDepth = PomShellDepth(u_viewProjectionMatrix, hit.position, rayDir, viewDistance + hit.t);
}
#endif

void main()
{
	vec3 viewDir, lightColor, ambientColor;
	vec3 L, N, E;

	vec2 texCoords = var_TexCoords.xy;
	vec2 lmCoords = var_TexCoords.zw;
#if defined(USE_SILHOUETTE_POM)
	vec3 pomViewDir;
	PomHit pomHit;
	bool pomShell;
	PomSilhouetteFragment(texCoords, lmCoords, pomViewDir, pomHit, pomShell);
#endif
#if defined(USE_SSR) || defined(USE_SSGI) || defined(USE_SKIN_SSS_BUFFER)
  #if defined(USE_SILHOUETTE_POM)
	SSRWriteNone(u_ViewOrigin - pomViewDir);
  #elif defined(PER_PIXEL_LIGHTING)
	SSRWriteNone(u_ViewOrigin - var_ViewDir.xyz);
  #else
	SSRWriteNone(var_Position);
  #endif
#endif
#if defined(PER_PIXEL_LIGHTING) && !defined(USE_SILHOUETTE_POM)
	// Unpack tangent view direction
	vec3 tangentViewDir = vec3(var_LightDir.w, var_Normal.w, var_ViewDir.w);
	vec2 tex_offset = GetParallaxOffset(texCoords, tangentViewDir);
	texCoords += tex_offset;
#endif

#if defined(USE_SILHOUETTE_POM)
	vec4 diffuse = textureGrad(u_DiffuseMap, texCoords, g_pomGradX, g_pomGradY);
#else
	vec4 diffuse = texture(u_DiffuseMap, texCoords);
#endif
	diffuse.a *= var_Color.a;
#if defined(USE_ALPHA_TEST)
	if (u_AlphaTestType == ALPHA_TEST_GT0)
	{
		if (diffuse.a == 0.0)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_LT128)
	{
		if (diffuse.a >= 0.5)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE128)
	{
		if (diffuse.a < 0.5)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE192)
	{
		if (diffuse.a < 0.75)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_E255)
	{
		if (diffuse.a < 1.00)
			discard;
	}
#endif

#if defined(PER_PIXEL_LIGHTING)
  #if defined(USE_SILHOUETTE_POM)
	viewDir = pomViewDir;
  #else
	viewDir = var_ViewDir.xyz;
  #endif
	E = normalize(viewDir);
	L = var_LightDir.xyz;
  #if defined(USE_DELUXEMAP)
	L += (texture(u_DeluxeMap, lmCoords).xyz - vec3(0.5)) * u_EnableTextures.y;
  #endif
#endif

#if defined(USE_LIGHTMAP)
	vec4 lightmapColor = texture(u_LightMap, lmCoords);
#endif

#if defined(PER_PIXEL_LIGHTING)
	float attenuation;

  #if defined(USE_LIGHTMAP)
	lightColor	= lightmapColor.rgb * var_Color.rgb;
	ambientColor = vec3 (0.0);
	attenuation = 1.0;
  #elif defined(USE_LIGHT_VECTOR)
	lightColor	= u_DirectedLight * var_Color.rgb;
	ambientColor = u_AmbientLight * var_Color.rgb;
	attenuation = 1.0;
  #elif defined(USE_LIGHT_VERTEX)
	lightColor	= var_Color.rgb;
	ambientColor = vec3 (0.0);
	attenuation = 1.0;
  #endif

  #if defined(USE_ENTITY_GRID)
	#if defined(USE_ENTITY_GPU_GRID)
	EntityGridSample gridGpu;
	#endif
	EntityGridSample gridMulti;
	#if defined(USE_ENTITY_GPU_GRID)
	if (u_GridParams.z > 1.5 || u_GridScale.w > 0.5)
		gridGpu = SampleEntityGrid(u_ViewOrigin - viewDir);
	#endif
	if ((u_GridParams.z > 0.5 && u_GridParams.z < 1.5) || u_GridScale.w > 0.5)
		gridMulti = SampleEntityMultiPoint((u_ViewOrigin - viewDir).z);
	if (u_GridParams.z > 0.5)
	{
		#if defined(USE_ENTITY_GPU_GRID)
		EntityGridSample selected = u_GridParams.z > 1.5 ? gridGpu : gridMulti;
		#else
		EntityGridSample selected = gridMulti;
		#endif
		L = selected.direction;
		lightColor = EntityGridDirectedCompatibility(selected.directed) * var_Color.rgb;
		ambientColor = EntityGridAmbientCompatibility(selected.ambient) * var_Color.rgb;
	}
  #endif
	float sqrLightDist = max(dot(L, L), 1e-12);

  #if defined(USE_SILHOUETTE_POM)
	// the base surface normal: shell walls face other directions
	vec3 vertexNormal = var_Normal.xyz * u_NormalScale.z;
  #else
	vec3 vertexNormal = mix(var_Normal.xyz, -var_Normal.xyz, float(gl_FrontFacing)) * u_NormalScale.z;
  #endif
	N = CalcNormal(vertexNormal, var_Tangent, texCoords);
	L /= sqrt(sqrLightDist);

  #if defined(USE_SKIN_SSS)
	g_skinScatter = u_SkinParams.x;
	if (g_skinScatter > 0.0)
	{
		if (u_SkinParams.y > 0.5)
			g_skinScatter *= texture(u_SkinMaskMap, texCoords).r;
		// r_skinSSSCompare: left half without the wrap / transmission
		bool skinLeft = gl_FragCoord.x < u_SkinParams.z;
		g_skinWrap = skinLeft ? vec3(0.0) : u_SkinWrap.rgb * g_skinScatter;
		g_skinTransmission = skinLeft ? 0.0 : u_SkinWrap.w * g_skinScatter;
	}
  #endif

  #if defined(USE_WETNESS)
	// Rain wetness: changes only the material inputs (normal here, albedo and
	// roughness below), before any lighting, so direct light, dynamic lights,
	// cubemap IBL, SSR and SSGI all see the same wet material.
	float rainExposure = 0.0;
	float wetness = 0.0;
	float puddleSlope = 0.0;
	float puddleField = 0.0;
	float puddle = 0.0;
	float puddleEdge = 0.0;
	float puddleMacro = 0.0;
	float puddleMacroEdge = 0.0;
	float puddleDepth = -1.0;	// material depth of the height aware path, < 0: none
	vec3 ripple = vec3(0.0);	// ungated rings: height, world slope
	float rippleMask = 0.0;
	vec2 rippleSlope = vec2(0.0);	// the slope applied to N
	float runoff = 0.0;			// water film / streak mask
	float runoffCore = 0.0;		// stream centres
	float runoffW = 0.0;		// slope class weight: 1 - puddle slope, no down facing
	float runoffExposure = 0.0;	// rain exposure with the wall probe
	vec3 runoffFlow = vec3(0.0);	// gravity projected on the surface
	vec2 runoffField = vec2(0.0);	// ungated pattern (debug 25)
	// pixel footprint of the undisplaced surface, taken in uniform control flow
	float rippleFootprint = length(fwidth((u_ViewOrigin - var_ViewDir.xyz).xy));
	if (u_WetnessParams.x > 0.0 || u_WetnessParams2.z > 0.0)
	{
		vec3 wetGeoNormal = normalize(vertexNormal);
		if (u_WetnessParams.x > 0.0 || u_WetnessParams2.z == 1.0)
			rainExposure = ComputeRainExposure(u_ViewOrigin - viewDir, wetGeoNormal, u_WetnessParams2.y);
		// walls get about half the rain, faces pointing down none
		// (entities: most of the side, u_WetnessParams3.x)
		float facing = mix(u_WetnessParams3.x, 1.0, clamp(wetGeoNormal.z, 0.0, 1.0)) * step(-0.2, wetGeoNormal.z);
		wetness = rainExposure * max(u_WetnessParams.x, 0.0) * facing;
		if (u_WetnessParams2.z == 4.0 && gl_FragCoord.x < u_WetnessParams2.w)
			wetness = 0.0;	// dry / wet split
		N = normalize(mix(N, wetGeoNormal, wetness * u_WetnessParams.w));

		// Puddles: rain exposure x flat geometric normal x world noise x
		// world-only eligibility. The normal map is not used for the slope.
		puddleSlope = smoothstep(u_PuddleParams.z, u_PuddleParams.w, wetGeoNormal.z);
		float exposureP = smoothstep(0.5, 1.0, rainExposure);
		bool puddleDebug = u_WetnessParams2.z >= 5.0 && u_WetnessParams2.z <= 20.0;
		if (u_PuddleParams.x > 0.0 && ((wetness > 0.0 && puddleSlope * exposureP > 0.0) || puddleDebug))
		{
			puddleField = PuddleField((u_ViewOrigin - viewDir).xy * u_PuddleParams2.x);
			float t = 1.0 - u_PuddleParams.x;
			float gate = puddleSlope * exposureP * step(0.0, u_WetnessParams.x);
			puddleMacro = smoothstep(t, t + 0.06, puddleField) * gate;
			puddleMacroEdge = smoothstep(t - 0.10, t, puddleField) * gate;
			puddle = puddleMacro;
			puddleEdge = puddleMacroEdge;
			// smooth water surface: underlying detail fades in the core
			float flatten = max(puddle, puddleEdge * 0.5);
    #if defined(USE_PARALLAXMAP)
			// the real height field of the shaded (POM displaced) point decides
			// where inside the macro puddle the water stands
			if (u_PuddleHeight.y > 0.0)
			{
				puddleDepth = PuddleRelief(SampleMaterialDepth(u_NormalMap, texCoords, g_pom.gradX, g_pom.gradY));
				float basin = clamp((puddleField - (t - 0.10)) / 0.16, 0.0, 1.0);
				vec3 micro = PuddleMicro(basin, puddleDepth) * gate;
				puddleEdge = micro.x;
				puddle = micro.y;
				flatten = micro.z;
			}
    #endif
			if (u_WetnessParams2.z == 4.0 && gl_FragCoord.x < u_WetnessParams2.w)
				puddle = puddleEdge = flatten = 0.0;
			N = normalize(mix(N, wetGeoNormal, flatten));

			// rain ripples: only on the submerged core, fading out before its
			// edge, so no ring reaches the fringe film or dry stone
			if (u_PuddleRipple.x > 0.0 && (puddle > 0.35 || (u_WetnessParams2.z >= 17.0 && u_WetnessParams2.z <= 20.0)))
			{
				ripple = PuddleRipples((u_ViewOrigin - viewDir).xy, rippleFootprint);
				rippleMask = smoothstep(0.35, 0.9, puddle);
				rippleSlope = ripple.yz * (u_PuddleRipple.x * rippleMask);
				// height field normal (-dh/dx, -dh/dy, 1), kept in the water plane
				vec3 tilt = vec3(-rippleSlope, 0.0);
				N = normalize(N + tilt - wetGeoNormal * dot(wetGeoNormal, tilt));
			}
		}

		// Runoff: where puddles fade out with the slope, a film runs down.
		// Classes blend smoothly: up facing flat -> puddle (puddleSlope),
		// sloped / near vertical -> runoff (the exact complement), down
		// facing -> fading out to none on ceilings.
		const vec3 gravity = vec3(0.0, 0.0, -1.0);
		runoffFlow = gravity - wetGeoNormal * dot(gravity, wetGeoNormal);
		float flowLength = length(runoffFlow);
		runoffFlow = flowLength > 0.05 ? runoffFlow / flowLength : vec3(0.0);
		runoffW = (1.0 - smoothstep(u_PuddleParams.z, u_PuddleParams.w, wetGeoNormal.z)) *
			smoothstep(0.05, 0.2, flowLength) * smoothstep(-0.35, 0.05, wetGeoNormal.z);
		bool runoffDebug = u_WetnessParams2.z >= 21.0 && u_WetnessParams2.z <= 26.0;
		if (u_RunoffParams.x > 0.0 && ((u_WetnessParams.x > 0.0 && runoffW > 0.0) || runoffDebug))
		{
			vec3 worldPosition = u_ViewOrigin - viewDir;
			// The weather map is top down: vertical rain on up facing
			// surfaces. A steep face also tests a column a little further out
			// (r_runoffProbe, <= 32 units): an exterior wall next to open sky
			// counts as exposed, a wall under a deeper roof stays dry.
			runoffExposure = rainExposure;
			float steep = 1.0 - smoothstep(0.35, 0.7, wetGeoNormal.z);
			if (u_RunoffParams.w > 0.0 && steep > 0.0 && runoffExposure < 1.0)
				runoffExposure = max(runoffExposure,
					steep * ComputeRainExposure(worldPosition, wetGeoNormal, u_RunoffParams.w));
			// windward faces run a little more, leeward ones less
			vec2 windDir = u_RunoffParams2.xy / max(length(u_RunoffParams2.xy), 1e-6);
			float windward = 1.0 + u_RunoffParams2.z * dot(-windDir, wetGeoNormal.xy);
			float gate = runoffW * smoothstep(0.25, 0.9, runoffExposure) * windward *
				u_RunoffParams.x * step(0.0, u_WetnessParams.x);
			if (gate > 0.0 || u_WetnessParams2.z == 25.0)
				runoffField = RunoffPattern(worldPosition, wetGeoNormal);
			runoff = clamp(runoffField.x * gate, 0.0, 1.0);
			runoffCore = clamp(runoffField.y * gate, 0.0, 1.0);
			if (u_WetnessParams2.z == 4.0 && gl_FragCoord.x < u_WetnessParams2.w)
				runoff = runoffCore = 0.0;
			// the film smooths the surface detail, most in the streams
			N = normalize(mix(N, wetGeoNormal, 0.4 * runoff + 0.4 * runoffCore));
			// a film is wet surface: the material response below (darkening,
			// roughness, per class) applies to it
			wetness = max(wetness, runoff);
		}
	}
  #endif

	// screen-space AO (r), sun contact shadow (g) and GTAO bent normal (ba) of
	// this view
	float AO = 1.0;
	float contactShadow = 1.0;
	#if defined (USE_SSAO)
	vec2 windowTex = gl_FragCoord.xy / r_FBufScale;
	vec4 screenAO = texture(u_SSAOMap, windowTex);
	AO = screenAO.r;
	contactShadow = screenAO.g;
	#if defined(USE_SILHOUETTE_POM)
	// contact shadows march the displaced depth: hard black grooves on shells
	if (pomShell && u_PomFade.w < 0.5)
		contactShadow = 1.0;
	#endif
	#endif
	float cascadeShadow = 1.0;
	#if defined(USE_SHADOWMAP) && defined(USE_SHADOWS2)
	SunCascadeResult sunInfo;
	#endif
	#if defined(USE_PARALLAXMAP)
	float pomSunShadow = 1.0;
	#endif

  #if defined(USE_SHADOWMAP)
	vec3 primaryLightDir = normalize(u_PrimaryLightOrigin.xyz);
	float NPL = clamp(dot(N, primaryLightDir), 0.0, 1.0);
	#if defined(USE_SILHOUETTE_POM)
	// shell hits look the shadow map up above their own relief
	vec3 shadowViewDir = viewDir;
	if (pomShell && pomHit.hit)
		shadowViewDir = u_ViewOrigin - PomShadowLookupPosition(pomHit.position, pomHit.depth,
			var_PomShell.y, g_pom.N, primaryLightDir);
	#endif
	#if defined(USE_SHADOWS2)
	vec3 geometricNormal = normalize(vertexNormal);
	float geometricNPL = clamp(dot(geometricNormal, primaryLightDir), 0.0, 1.0);
	#if defined(USE_SILHOUETTE_POM)
	sunInfo = sunShadowModern(u_ViewOrigin - shadowViewDir, geometricNormal, geometricNPL);
	#else
	sunInfo = sunShadowModern(u_ViewOrigin - viewDir, geometricNormal, geometricNPL);
	#endif
	cascadeShadow = sunInfo.visibility;
	#else
	vec3 normalBias = vertexNormal * (1.0 - NPL);
	#if defined(USE_SILHOUETTE_POM)
	cascadeShadow = sunShadow(u_ViewOrigin, shadowViewDir, normalBias, u_ShadowMap);
	#else
	cascadeShadow = sunShadow(u_ViewOrigin, viewDir, normalBias, u_ShadowMap);
	#endif
	#endif
	// contact shadows only refine the near field of the cascaded shadow map
	float shadowValue = cascadeShadow * contactShadow * NPL;
	#if defined(USE_PARALLAXMAP)
	// POM self shadow: part of the sun visibility, so it applies wherever the
	// sun shadow does (r_sunlightMode 1 lightmap modulation, 2 direct sun)
	pomSunShadow = GetPomSelfShadow(PomSunDirection(primaryLightDir));
	shadowValue *= pomSunShadow;
	#endif

    #if defined(SHADOWMAP_MODULATE)
	vec3 ambientScale = mix(vec3(1.0), u_PrimaryLightAmbient, u_EnableTextures.z);
	lightColor = mix(ambientScale * lightColor, lightColor, shadowValue);
    #endif
  #endif

  #if defined(USE_LIGHTMAP) || defined(USE_LIGHT_VERTEX)
	ambientColor = lightColor;
	float surfNL = clamp(dot(vertexNormal, L), 0.0, 1.0);

	// Scale the incoming light to compensate for the baked-in light angle
	// attenuation.
	lightColor /= max(surfNL, 0.25);

	// Recover any unused light as ambient, in case attenuation is over 4x or
	// light is below the surface
	ambientColor = max(ambientColor - lightColor * surfNL, 0.0);
  #endif

	// Lambert and Burley diffuse both contain 1 / PI. Compensate it here to
	// preserve Rend2's legacy lightmap, vertex-light and light-grid intensity.
	// Dynamic lights apply the same compensation to their diffuse term locally.
	lightColor *= M_PI;

	// Dont scale ambient as we dont compute lambertian diffuse for it
	// We dont compute it because cloth diffuse is dependent on NL
	// So we just skip this. Reconsider this again when more BRDFS are added

	vec4 specular = vec4(1.0);
	float roughness = 0.99;
  #if defined(USE_SPECULARMAP)
  #if !defined(USE_SPECGLOSS)
    #if defined(USE_SILHOUETTE_POM)
	vec4 ORMS = textureGrad(u_SpecularMap, texCoords, g_pomGradX, g_pomGradY);
    #else
	vec4 ORMS = texture(u_SpecularMap, texCoords);
    #endif
	ORMS.xyzw *= u_SpecularScale.zwxy;

	specular.rgb = mix(vec3(0.08) * ORMS.w, diffuse.rgb, ORMS.z);
	diffuse.rgb *= vec3(1.0 - ORMS.z);

	roughness = mix(0.01, 1.0, ORMS.y);
	AO = min(ORMS.x, AO);
  #else
    #if defined(USE_SILHOUETTE_POM)
	specular = textureGrad(u_SpecularMap, texCoords, g_pomGradX, g_pomGradY);
    #else
	specular = texture(u_SpecularMap, texCoords);
    #endif
	specular.rgb *= u_SpecularScale.xyz;
	roughness = mix(1.0, 0.01, specular.a * (1.0 - u_SpecularScale.w));
  #endif
  #endif

  #if defined(USE_WETNESS)
	if (wetness > 0.0)
	{
		// a water film darkens porous (rough, dielectric) albedo and smooths
		// the surface; F0 is kept: dielectrics stay dielectric, metals metal.
		// diffuse is already (1 - metal) scaled on the ORMS path.
		// generic class: physical porosity from the material; other classes
		// (cloth, armor, ...) use their darkening as is
		float porosity = roughness;
    #if defined(USE_SPECULARMAP) && !defined(USE_SPECGLOSS)
		porosity *= 1.0 - ORMS.z;
    #endif
		porosity = mix(1.0, porosity, u_WetnessParams3.y);
		diffuse.rgb *= 1.0 - wetness * u_WetnessParams.z * porosity;
		roughness = mix(roughness, max(roughness * u_WetnessParams.y, 0.08), wetness);
	}
	if (runoffCore > 0.0)
	{
		// running streams: a thicker, smoother film than the wet surface
		// around them (F0 kept, no tint, no emission)
		roughness = mix(roughness, max(roughness * 0.5, 0.06), runoffCore);
		diffuse.rgb *= 1.0 - 0.08 * runoffCore;
	}
	if (puddleEdge > 0.0)
	{
		// standing water: fringe intermediate, core near mirror; F0 kept
		// (no metal, no tint), the dielectric Fresnel / IBL / SSR reflect it
		float metal = 0.0;
    #if defined(USE_SPECULARMAP) && !defined(USE_SPECGLOSS)
		metal = ORMS.z;
    #endif
		float fringeRough = min(roughness, mix(roughness, u_PuddleParams.y, 0.5));
		roughness = mix(roughness, fringeRough, puddleEdge);
		roughness = mix(roughness, u_PuddleParams.y, puddle);
		diffuse.rgb *= 1.0 - 0.25 * puddle * (1.0 - metal);
	}
  #endif

	// Environment reflections. Legacy application: the F0 term is scaled by AO
	// and the cubemap brightness follows the AO'd lighting (kept bit exact for
	// A/B). Indirect-only: the whole IBL result is scaled by the specular
	// occlusion, the cubemap brightness follows the unoccluded lighting.
	vec3 specularAO = specular.rgb * AO;
	float specOcclusion = 1.0;
	bool unoccludedIBL = false;
	vec3 unoccludedLighting = lightColor + ambientColor;
	// direction of indirect (environment) light: bent towards the unoccluded
	// directions by GTAO, never used for direct light
	vec3 indirectN = N;
#if defined(USE_SSAO)
	vec3 ambientVisibility = vec3(AO);
	bool indirectOnlyAO = u_AOParams.x == 1.0 ||
		(u_AOParams.x == 2.0 && gl_FragCoord.x >= u_AOParams.w);
	if (indirectOnlyAO)
	{
		// AO is the loss of indirect light: it attenuates ambient light, the
		// share of baked lighting that is indirect, and (as specular
		// occlusion) the environment reflections, never real-time direct
		// light (sun, dynamic lights, light grid directed light)
		if (u_AOParams2.z > 0.0)
		{
			// The bent normal comes from the depth buffer: add its offset from
			// the geometric normal to the (normal mapped) shading normal. Only
			// where the screen AO sees occlusion; views without GTAO sample
			// the white image (AO 1), so their ba is never used.
			float bentWeight = u_AOParams2.z * clamp((1.0 - screenAO.r) * 2.0, 0.0, 1.0);
			vec3 geometricN = normalize(vertexNormal);
			geometricN = dot(geometricN, E) < 0.0 ? -geometricN : geometricN;
			vec3 bentN = AODecodeBentNormal(screenAO.ba);
			indirectN = normalize(N + bentWeight * (bentN - geometricN));
		}
		if (u_AOParams.z > 0.0)
			ambientVisibility = AOMultiBounce(AO, diffuse.rgb);
		ambientColor *= ambientVisibility;
    #if defined(USE_LIGHTMAP) || defined(USE_LIGHT_VERTEX)
		lightColor *= mix(vec3(1.0), ambientVisibility, u_AOParams.y);
    #endif
		specularAO = specular.rgb;
		specOcclusion = AOSpecularOcclusion(N, E, indirectN, abs(dot(N, E)) + 1e-5, AO, roughness);
		unoccludedIBL = true;
	}
	else
#endif
	ambientColor *= AO;
	vec3 iblLighting = unoccludedIBL ? unoccludedLighting : lightColor + ambientColor;

	// The cubemap provides only an angular distribution. Keep the light-grid
	// ambient as the energy source; never add the captured world's full light.
	vec3 diffuseAmbientColor = ambientColor;
#if defined(USE_DIFFUSE_IBL) && defined(USE_LIGHT_VECTOR)
	vec3 probeIrradiance = vec3(0.0);
	vec3 directionalFactor = vec3(1.0);
	if (u_DiffuseIBLParams.w > 0.5)
	{
		probeIrradiance = max(texture(u_DiffuseIrradianceMap, indirectN).rgb, vec3(0.0));
		vec3 averageIrradiance = max(texelFetch(u_ProbeAverageMap,
			ivec2(int(u_DiffuseIBLParams.z), 0), 0).rgb, vec3(0.0));
		float averageLuma = dot(averageIrradiance, vec3(0.2126, 0.7152, 0.0722));
		if (averageLuma > 1e-4)
		{
			// The sphere average of cosine-convolved radiance equals the
			// sphere average of radiance, so both textures have matching units.
			vec3 safeAverage = max(averageIrradiance, vec3(averageLuma * 0.05));
			directionalFactor = clamp(probeIrradiance / safeAverage,
				vec3(0.25), vec3(4.0));
		}
		diffuseAmbientColor *= mix(vec3(1.0), directionalFactor,
			clamp(u_DiffuseIBLParams.x, 0.0, 1.0));
	}
#endif

	vec3  H  = normalize(L + E);
	float NE = abs(dot(N, E)) + 1e-5;
	float NL = clamp(dot(N, L), 0.0, 1.0);
	float LH = clamp(dot(L, H), 0.0, 1.0);

	vec3  Fd = CalcDiffuse(diffuse.rgb, NE, NL, LH, roughness);
	vec3  Fs = vec3(0.0);

  #if defined(USE_SPECULARMAP)
  #if defined(USE_LIGHT_VECTOR)
	float NH = clamp(dot(N, H), 0.0, 1.0);
	float VH = clamp(dot(E, H), 0.0, 1.0);
	Fs = CalcSpecular(specular.rgb, NH, NL, NE, LH, VH, roughness);
  #endif

  #if ((defined(USE_LIGHTMAP) && defined(USE_DELUXEMAP)) || defined(USE_LIGHT_VERTEX)) && defined(r_deluxeSpecular)
	float NH = clamp(dot(N, H), 0.0, 1.0);
	float VH = clamp(dot(E, H), 0.0, 1.0);
	Fs = CalcSpecular(specular.rgb, NH, NL, NE, LH, VH, roughness) * r_deluxeSpecular;
  #endif
  #endif

	vec3 reflectance = Fd + Fs;

#if defined(USE_SKIN_SSS)
	vec3 skinDirect = lightColor * Fd * (attenuation * SkinDiffuseNL(dot(N, L), NL)) +
		SkinTransmission(N, E, L, lightColor * attenuation, diffuse.rgb);
	g_skinDiffuse += skinDirect + diffuseAmbientColor * diffuse.rgb;
	out_Color.rgb  = skinDirect + lightColor * Fs * (attenuation * NL);
#else
	out_Color.rgb  = lightColor * reflectance * (attenuation * NL);
#endif
	out_Color.rgb += diffuseAmbientColor * diffuse.rgb;

	// kept separately: r_forwardPlusDebug, later SSGI style consumers
	vec3 dynamicLight = CalcDynamicLightContribution(roughness, N, E, u_ViewOrigin, viewDir, NE, diffuse.rgb, specular.rgb, vertexNormal);
	out_Color.rgb += dynamicLight;
#if defined(USE_SSR)
	vec3 cubemapReflection = CalcIBLContribution(roughness, N, E, u_ViewOrigin, viewDir, NE, specularAO, iblLighting) * specOcclusion;
	out_Color.rgb += cubemapReflection;
  #if defined(USE_SPECULARMAP)
	out_SSRNormal = vec4(SSREncodeNormal(N), roughness, 1.0);
	// SSR hits are traced geometry, not occluded again (indirect-only: the
	// weight has no specular occlusion; ssr_composite removes the occluded
	// cubemap where SSR hits)
	out_SSRSpecular = vec4(sqrt(clamp(SSRSpecularWeight(roughness, NE, specularAO), 0.0, 1.0)), 0.0);
	out_SSRCubemap.rgb = cubemapReflection;
  #endif
#else
	out_Color.rgb += CalcIBLContribution(roughness, N, E, u_ViewOrigin, viewDir, NE, specularAO, iblLighting) * specOcclusion;
#endif
#if defined(USE_SSGI)
	SSGIWriteReceiver(N, roughness, diffuse.rgb);
#endif

  #if defined(USE_PRIMARY_LIGHT)
	vec3  L2   = normalize(u_PrimaryLightOrigin.xyz);
	vec3  H2   = normalize(L2 + E);
	float NL2  = clamp(dot(N,  L2), 0.0, 1.0);
	float L2H2 = clamp(dot(L2, H2), 0.0, 1.0);
	float NH2  = clamp(dot(N,  H2), 0.0, 1.0);
	float VH2  = clamp(dot(E, H), 0.0, 1.0);
    #if defined(USE_SKIN_SSS)
	vec3 sunDiffuse = CalcDiffuse(diffuse.rgb, NE, NL2, L2H2, roughness);
	reflectance = CalcSpecular(specular.rgb, NH2, NL2, NE, L2H2, VH2, roughness);
    #else
	reflectance  = CalcDiffuse(diffuse.rgb, NE, NL2, L2H2, roughness);
	reflectance += CalcSpecular(specular.rgb, NH2, NL2, NE, L2H2, VH2, roughness);
    #endif

	lightColor = u_PrimaryLightColor;
    #if defined(USE_SHADOWMAP)
	lightColor *= shadowValue;
    #endif

    #if defined(USE_SKIN_SSS)
	vec3 skinSun = lightColor * sunDiffuse * SkinDiffuseNL(dot(N, L2), NL2);
	g_skinDiffuse += skinSun;
	out_Color.rgb += skinSun + lightColor * reflectance * NL2;
    #else
	out_Color.rgb += lightColor * reflectance * NL2;
    #endif
  #endif

  #if defined(USE_SHADOWMAP) && defined(USE_SHADOWS2)
	// r_shadowDebug 1-11. These values are deliberately written unlit; the
	// post-process path bypasses tone mapping while a shadow debug view is on.
	if (u_ShadowDebug.x >= 1.0)
	{
		vec3 debugColor;
		if (u_ShadowDebug.x == 1.0)
		{
			vec3 c0 = vec3(1.0, 0.18, 0.12);
			vec3 c1 = vec3(0.15, 1.0, 0.2);
			vec3 c2 = vec3(0.15, 0.35, 1.0);
			debugColor = sunInfo.cascade < 1.0 ?
				mix(c0, c1, sunInfo.cascade) : mix(c1, c2, sunInfo.cascade - 1.0);
		}
		else if (u_ShadowDebug.x == 2.0)
			debugColor = vec3(sunInfo.rawDepth);
		else if (u_ShadowDebug.x == 3.0)
			debugColor = vec3(sunInfo.fixedPcf);
		else if (u_ShadowDebug.x == 4.0)
			debugColor = vec3(sunInfo.blockerDepth);
		else if (u_ShadowDebug.x == 5.0)
			debugColor = vec3(sunInfo.penumbraWorld / max(u_ShadowPcss.y, 1e-5));
		else if (u_ShadowDebug.x == 6.0)
			debugColor = vec3(sunInfo.visibility);
		else if (u_ShadowDebug.x == 7.0)
			debugColor = vec3(contactShadow);
		else if (u_ShadowDebug.x == 8.0)
			debugColor = vec3(sunInfo.visibility * contactShadow);
		else if (u_ShadowDebug.x == 10.0)
		{
			// lowest dynamic light cube shadow visibility (1 without cubes)
			#if defined(USE_DSHADOWS)
			debugColor = vec3(g_dlightShadowVisibility);
			#else
			debugColor = vec3(1.0);
			#endif
		}
		else if (u_ShadowDebug.x == 11.0)
		{
			// skinned (Ghoul2) receivers magenta, everything else grey: shows
			// Ghoul2 self shadowing and the shadows Ghoul2 casts on the world
			#if defined(USE_SKELETAL_ANIMATION)
			debugColor = vec3(1.0, 0.25, 1.0) * (0.15 + 0.85 * sunInfo.visibility * contactShadow);
			#else
			debugColor = vec3(0.6) * (0.15 + 0.85 * sunInfo.visibility * contactShadow);
			#endif
		}
		else
			debugColor = vec3(sunInfo.biasWorld / max(u_ShadowBias.w, 1e-5));

		out_Color = vec4(debugColor, diffuse.a);
		out_Glow = vec4(0.0, 0.0, 0.0, diffuse.a);
		return;
	}
  #endif

#if defined(USE_DIFFUSE_IBL) && defined(USE_LIGHT_VECTOR)
	if (u_DiffuseIBLParams.y >= 1.0 && u_DiffuseIBLParams.y <= 5.0)
	{
		vec3 debugColor;
		if (u_DiffuseIBLParams.y == 1.0)
			debugColor = probeIrradiance;
		else if (u_DiffuseIBLParams.y == 2.0)
			debugColor = directionalFactor * 0.5; // neutral response = middle gray
		else if (u_DiffuseIBLParams.y == 3.0)
			debugColor = ambientColor * diffuse.rgb;
		else if (u_DiffuseIBLParams.y == 4.0)
			debugColor = diffuseAmbientColor * diffuse.rgb;
		else if (u_DiffuseIBLParams.w > 0.5)
		{
			float id = u_DiffuseIBLParams.z + 1.0;
			debugColor = fract(sin(id * vec3(12.9898, 78.233, 39.3467)) * 43758.5453);
		}
		else
			debugColor = vec3(1.0, 0.0, 1.0); // legacy fallback: no probe
		out_Color = vec4(debugColor, diffuse.a);
		out_Glow = vec4(0.0, 0.0, 0.0, diffuse.a);
    #if defined(USE_SSR) && defined(USE_SPECULARMAP)
		out_SSRSpecular = vec4(0.0);
		out_SSRCubemap.rgb = vec3(0.0);
    #endif
		return;
	}
#endif

#if defined(USE_LTC_DEBUG) && defined(PER_PIXEL_LIGHTING)
	// r_ltcDebug 1-5, 8, written unlit (tone mapping is bypassed)
	vec3 ltcDebugColor;
	if (LtcDebugColor(u_ViewOrigin - viewDir, out_Color.rgb, ltcDebugColor))
	{
		out_Color = vec4(ltcDebugColor, diffuse.a);
		out_Glow = vec4(0.0, 0.0, 0.0, diffuse.a);
    #if defined(USE_SSR) && defined(USE_SPECULARMAP)
		out_SSRSpecular = vec4(0.0);
		out_SSRCubemap.rgb = vec3(0.0);
    #endif
		return;
	}
#endif

	// r_forwardPlusDebug 1-9, written unlit (tone mapping is bypassed)
	vec3 fplusDebugColor;
	if (FPlusDebugColor(u_ViewOrigin - viewDir, out_Color.rgb, dynamicLight, fplusDebugColor))
	{
		out_Color = vec4(fplusDebugColor, diffuse.a);
		out_Glow = vec4(0.0, 0.0, 0.0, diffuse.a);
    #if defined(USE_SSR) && defined(USE_SPECULARMAP)
		out_SSRSpecular = vec4(0.0);
		out_SSRCubemap.rgb = vec3(0.0);
    #endif
		return;
	}

  #if defined(USE_ENTITY_GPU_GRID)
	if (u_GridScale.w > 0.5)
	{
		vec3 legacy = u_AmbientLight + u_DirectedLight * max(dot(N, normalize(var_LightDir.xyz)), 0.0);
		vec3 multi = EntityGridAmbientCompatibility(gridMulti.ambient) +
			EntityGridDirectedCompatibility(gridMulti.directed) * max(dot(N, gridMulti.direction), 0.0);
		vec3 gpu = EntityGridAmbientCompatibility(gridGpu.ambient) +
			EntityGridDirectedCompatibility(gridGpu.directed) * max(dot(N, gridGpu.direction), 0.0);
		vec3 debugColor = vec3(0.0);
		if (u_GridScale.w == 1.0) debugColor = gridGpu.ambient;
		else if (u_GridScale.w == 2.0) debugColor = gridGpu.directed;
		else if (u_GridScale.w == 3.0) debugColor = gridGpu.direction * 0.5 + 0.5;
		else if (u_GridScale.w == 4.0) debugColor = vec3(gridGpu.validity);
		else if (u_GridScale.w == 5.0) debugColor = fract(gridGpu.cell * 0.125);
		else if (u_GridScale.w == 6.0) debugColor = legacy;
		else if (u_GridScale.w == 7.0) debugColor = multi;
		else if (u_GridScale.w == 8.0) debugColor = gpu;
		else if (u_GridScale.w == 9.0) debugColor = min(abs(gpu - legacy) * 4.0, vec3(1.0));
		out_Color = vec4(debugColor, diffuse.a);
		out_Glow = vec4(0.0, 0.0, 0.0, diffuse.a);
    #if defined(USE_SSR) && defined(USE_SPECULARMAP)
		out_SSRSpecular = vec4(0.0);
		out_SSRCubemap.rgb = vec3(0.0);
    #endif
		return;
	}
  #endif

  #if defined(USE_WETNESS)
	// r_weatherWetnessDebug 1-26 (not 4), written unlit (tone mapping is bypassed)
	if (u_WetnessParams2.z >= 1.0 && u_WetnessParams2.z <= 26.0 && u_WetnessParams2.z != 4.0)
	{
		float shade = 0.35 + 0.65 * NE;
		vec3 debugColor;
		// 11-15: material depth of the shaded point (raw, and rescaled to the
		// relief), < 0 without usable height (no normalHeightMap, flat height
		// or r_puddleHeight 0): magenta in 11, 12, 14
		float debugRawDepth = -1.0;
		float debugDepth = -1.0;
    #if defined(USE_PARALLAXMAP)
		if (u_PuddleHeight.y > 0.0)
		{
			debugRawDepth = SampleMaterialDepth(u_NormalMap, texCoords, g_pom.gradX, g_pom.gradY);
			debugDepth = PuddleRelief(debugRawDepth);
		}
    #endif
		if (u_WetnessParams2.z >= 11.0 && u_WetnessParams2.z <= 15.0 && u_WetnessParams2.z != 13.0 && u_WetnessParams2.z != 15.0 && debugDepth < 0.0)
			debugColor = vec3(1.0, 0.0, 1.0) * shade;
		else if (u_WetnessParams2.z == 11.0)	// raw sampled height, white = 1 (top of the 0..1 range)
			debugColor = vec3(1.0 - debugRawDepth);
		else if (u_WetnessParams2.z == 12.0)	// interpreted relief: deepest dark blue, peaks orange
			debugColor = mix(vec3(0.02, 0.05, 0.4), vec3(1.0, 0.55, 0.1), 1.0 - debugDepth);
		else if (u_WetnessParams2.z == 13.0)	// macro puddle mask only
			debugColor = mix(mix(vec3(0.25), vec3(0.2, 0.9, 0.9), puddleMacroEdge),
				vec3(0.05, 0.2, 1.0), puddleMacro) * shade;
		else if (u_WetnessParams2.z == 14.0)	// micro mask at a half filled basin, ungated
		{
    #if defined(USE_PARALLAXMAP)
			vec3 micro = PuddleMicro(0.5, debugDepth);
			debugColor = mix(mix(vec3(0.15 + 0.5 * (1.0 - debugDepth)), vec3(0.2, 0.9, 0.9), micro.x),
				vec3(0.05, 0.2, 1.0), micro.y);
    #else
			debugColor = vec3(1.0, 0.0, 1.0);
    #endif
		}
		else if (u_WetnessParams2.z == 15.0)	// combined puddle over the relief
			debugColor = mix(mix(vec3(0.15 + 0.5 * (1.0 - max(puddleDepth, 0.0))), vec3(0.2, 0.9, 0.9), puddleEdge),
				vec3(0.05, 0.2, 1.0), puddle);
		else if (u_WetnessParams2.z == 17.0)	// ripple height, ungated (+-0.5 = peak), bluish on puddles
			debugColor = mix(vec3(0.5 + ripple.x * u_PuddleRipple.y * 22.4),
				vec3(0.1, 0.35, 1.0), 0.3 * puddle);
		else if (u_WetnessParams2.z == 18.0)	// slope applied to N: red = x, green = y
			debugColor = vec3(clamp(0.5 + 1.5 * rippleSlope, 0.0, 1.0), 0.5);
		else if (u_WetnessParams2.z == 19.0)	// masking: puddle blue, applied rings yellow, suppressed rings red
		{
			debugColor = mix(vec3(0.25), vec3(0.05, 0.2, 1.0), puddle) * shade;
			debugColor = mix(debugColor, vec3(0.7, 0.1, 0.1),
				0.7 * clamp(4.0 * u_PuddleRipple.x * length(ripple.yz) * (1.0 - rippleMask), 0.0, 1.0));
			debugColor = mix(debugColor, vec3(1.0, 0.9, 0.1), clamp(4.0 * length(rippleSlope), 0.0, 1.0));
		}
		else if (u_WetnessParams2.z == 20.0)	// final effective normal
			debugColor = N * 0.5 + 0.5;
		else if (u_WetnessParams2.z == 21.0)	// geometric normal
			debugColor = normalize(vertexNormal) * 0.5 + 0.5;
		else if (u_WetnessParams2.z >= 22.0 && u_WetnessParams2.z != 25.0 && u_WetnessParams2.z != 26.0 && u_RunoffParams.x <= 0.0)
			// runoff off (grey) or excluded draw (magenta)
			debugColor = (u_RunoffParams.x < 0.0 ? vec3(1.0, 0.0, 1.0) : vec3(0.25)) * shade;
		else if (u_WetnessParams2.z == 22.0)	// slope class: puddle blue, slope green, steep yellow, down facing red
		{
			float c = normalize(vertexNormal).z;
			vec3 runColor = mix(vec3(0.1, 0.8, 0.1), vec3(1.0, 0.85, 0.1), 1.0 - smoothstep(0.35, 0.7, c));
			debugColor = mix(vec3(0.05, 0.2, 1.0), runColor, 1.0 - smoothstep(u_PuddleParams.z, u_PuddleParams.w, c));
			debugColor = mix(vec3(0.8, 0.1, 0.1), debugColor, smoothstep(-0.35, 0.05, c)) * shade;
		}
		else if (u_WetnessParams2.z == 23.0)	// flow direction (gravity on the surface), black where flat
			debugColor = (runoffFlow * 0.5 + 0.5) * smoothstep(0.0, 0.1, length(runoffFlow));
		else if (u_WetnessParams2.z == 24.0)	// runoff mask, red: exposure added by the wall probe
		{
			debugColor = mix(vec3(0.25) * shade, vec3(0.1, 0.6, 1.0), runoff);
			debugColor = mix(debugColor, vec3(0.9, 0.9, 1.0), runoffCore * 0.6);
			debugColor = mix(debugColor, vec3(0.9, 0.1, 0.1), 0.6 * clamp(runoffExposure - rainExposure, 0.0, 1.0));
		}
		else if (u_WetnessParams2.z == 25.0)	// ungated animated flow field: film grey, streams cyan
			debugColor = mix(vec3(runoffField.x), vec3(0.1, 0.9, 1.0), runoffField.y);
		else if (u_WetnessParams2.z == 26.0)	// final roughness
			debugColor = vec3(roughness);
		else if (u_WetnessParams2.z == 1.0)
			debugColor = vec3(rainExposure) * shade;
		else if (u_WetnessParams2.z == 2.0)
			debugColor = u_WetnessParams.x < 0.0 ? vec3(1.0, 0.0, 1.0) * shade :
				mix(vec3(0.25), vec3(0.1, 0.35, 1.0), wetness) * shade;
		else if (u_WetnessParams2.z == 3.0 || u_WetnessParams2.z == 9.0)
			debugColor = vec3(roughness);
		else if (u_WetnessParams2.z == 5.0)
			debugColor = mix(vec3(0.6, 0.1, 0.1), vec3(0.1, 0.8, 0.1), puddleSlope) * shade;
		else if (u_WetnessParams2.z == 6.0)
			debugColor = u_PuddleParams.x > 0.0 ? mix(vec3(puddleField),
				vec3(0.1, 0.35, 1.0), 0.5 * step(1.0 - u_PuddleParams.x, puddleField)) : vec3(0.25) * shade;
		else if (u_WetnessParams2.z == 7.0)
			debugColor = vec3(smoothstep(0.5, 1.0, rainExposure) * puddleSlope) * shade;
		else if (u_WetnessParams2.z == 8.0)
			debugColor = mix(mix(vec3(0.25), vec3(0.2, 0.9, 0.9), puddleEdge),
				vec3(0.05, 0.2, 1.0), puddle) * shade;
		else if (u_WetnessParams2.z == 16.0)	// wet response class, r_autoPBRDebug 1 colors
		{
			int cls = int(u_WetnessParams3.z + 0.5);
			vec3 classColors[7] = vec3[7](vec3(0.55), vec3(1.0, 0.85, 0.1), vec3(1.0, 0.5, 0.4),
				vec3(0.2, 0.4, 1.0), vec3(0.55, 0.28, 0.08), vec3(0.15, 0.9, 0.3), vec3(0.8, 0.15, 0.9));
			debugColor = (u_WetnessParams.x < 0.0 ? vec3(1.0, 0.0, 1.0) : classColors[clamp(cls, 0, 6)]) * shade;
		}
		else // 10: eligibility
			debugColor = (u_PuddleParams.x < 0.0 ? vec3(1.0, 0.0, 1.0) :
				u_PuddleParams.x > 0.0 ? vec3(0.1, 0.8, 0.1) : vec3(0.25)) * shade;
		out_Color = vec4(debugColor, diffuse.a);
		out_Glow = vec4(0.0, 0.0, 0.0, diffuse.a);
    #if defined(USE_SSR) && defined(USE_SPECULARMAP)
		out_SSRSpecular = vec4(0.0);
		out_SSRCubemap.rgb = vec3(0.0);
    #endif
		return;
	}
  #endif

	// r_autoPBRDebug 1-2, flat color with a little view facing shading so the
	// shape stays readable; written unlit (tone mapping is bypassed)
	// r_leafFlutterDebug 4 / 8 use the same view (tr_leafflutter.cpp)
	if (u_MaterialDebug.a > 0.0)
	{
		vec3 debugColor = u_MaterialDebug.rgb;
		if (u_LeafFlutterDebug == 8.0)
			debugColor = mix(vec3(0.05, 0.1, 0.8), vec3(1.0, 0.85, 0.1), var_LeafFlutter);
		out_Color = vec4(debugColor * (0.35 + 0.65 * NE), diffuse.a);
		out_Glow = vec4(0.0, 0.0, 0.0, diffuse.a);
    #if defined(USE_SSR) && defined(USE_SPECULARMAP)
		out_SSRSpecular = vec4(0.0);
		out_SSRCubemap.rgb = vec3(0.0);
    #endif
		return;
	}

  #if defined(USE_PARALLAXMAP) && defined(USE_POM_DEBUG)
	// r_pomDebug 1-8 (compiled with r_pomDebug set at renderer start), written unlit (tone mapping is bypassed)
	int pomView = int(u_PomDebug.w);
	if (pomView >= 1)
	{
		float shade = 0.35 + 0.65 * NE;
		vec3 debugColor;
		if (pomView == 1)	// raw height at the undisplaced coordinate
			debugColor = vec3(1.0 - textureGrad(u_NormalMap, var_TexCoords.xy, g_pom.gradX, g_pom.gradY).r);
		else if (pomView == 2)	// displaced texture coordinate
			debugColor = vec3(fract(texCoords), 0.0);
		else if (pomView == 3)	// view ray hit depth, white = top
			debugColor = g_pom.valid ? vec3(1.0 - g_pom.depth) : vec3(0.3, 0.0, 0.3);
		else if (pomView == 4)	// sun self shadow (1 without a sun shadow map)
			debugColor = vec3(pomSunShadow) * shade;
		else if (pomView == 5)	// darkest self shadow of the dynamic lights
			debugColor = vec3(g_pomLocalShadow) * shade;
		else if (pomView == 6)	// view ray height samples
			debugColor = g_pom.valid ? PomDebugHeat(g_pom.viewSamples / 74.0) : vec3(0.3) * shade;
		else if (pomView == 7)	// self shadow height samples, all lights
			debugColor = PomDebugHeat(g_pom.shadowSamples / (3.0 * max(u_PomShadow.y, 1.0)));
		else	// distance fade
			debugColor = PomDebugHeat(g_pom.fade) * shade;
		out_Color = vec4(debugColor, 1.0);
		out_Glow = vec4(0.0, 0.0, 0.0, 1.0);
    #if defined(USE_SSR) && defined(USE_SPECULARMAP)
		out_SSRSpecular = vec4(0.0);
		out_SSRCubemap.rgb = vec3(0.0);
    #endif
		return;
	}
  #endif

  #if defined(USE_SILHOUETTE_POM)
	// r_pomSilhouetteDebug 4-8, 10, 11 (1-3 are overlays, 9 is the crossfade
	// split), written unlit (tone mapping is bypassed)
	int pomDebug = int(u_PomParams2.w);
	if (pomDebug >= 4 && pomDebug != 9)
	{
		float shade = 0.35 + 0.65 * NE;
		bool pomWall = pomShell && PomIsWall(var_PomHeader);
		vec3 debugColor;
		if (pomDebug == 4)	// top cap green, walls orange, base surfaces in the crossfade band blue
			debugColor = (pomShell ? (pomWall ? vec3(1.0, 0.55, 0.1) : vec3(0.2, 0.9, 0.3)) : vec3(0.3, 0.4, 1.0)) * shade;
		else if (pomDebug == 5)	// pixels drawn through a boundary wall
			debugColor = pomWall ? vec3(1.0, 0.15, 0.1) : vec3(0.3) * shade;
		else if (pomDebug == 6)	// shell pixels the ray missed (discarded otherwise)
			debugColor = (pomShell && !pomHit.hit) ? vec3(1.0, 0.0, 1.0) : vec3(0.3) * shade;
		else if (pomDebug == 7)	// virtual hit distance, 32 unit bands
			debugColor = PomHeatColor(fract(length(viewDir) / 32.0)) * shade;
		else if (pomDebug == 8)	// height samples of the ray
			debugColor = pomShell ? PomHeatColor(pomHit.samples / (u_PomParams.y + u_PomParams.z + 1.0)) : vec3(0.3) * shade;
		else if (pomDebug == 10)	// linear view depth, 2048 units
			debugColor = vec3(clamp(dot(-viewDir, normalize(u_ViewForward)) / 2048.0, 0.0, 1.0));
		else	// material normal
			debugColor = N * 0.5 + 0.5;
		out_Color = vec4(debugColor, 1.0);
		out_Glow = vec4(0.0, 0.0, 0.0, 1.0);
    #if defined(USE_SSR) && defined(USE_SPECULARMAP)
		out_SSRSpecular = vec4(0.0);
		out_SSRCubemap.rgb = vec3(0.0);
    #endif
		return;
	}
  #endif

  #if defined(USE_SSAO)
	// r_debugAO 7-9, 12, written unlit (tone mapping is bypassed for these)
	if (u_AOParams2.x >= 7.0)
	{
		if (u_AOParams2.x == 7.0)
			out_Color.rgb = vec3(cascadeShadow);
		else if (u_AOParams2.x == 8.0)
		{
			// combined sun visibility (without N.L)
			float sunVisibility = cascadeShadow * contactShadow;
		#if defined(USE_PARALLAXMAP)
			sunVisibility *= pomSunShadow;
		#endif
			out_Color.rgb = vec3(sunVisibility);
		}
		else if (u_AOParams2.x == 12.0)
			out_Color.rgb = vec3(indirectOnlyAO ? specOcclusion : AO);
		else
			out_Color.rgb = indirectOnlyAO ? ambientVisibility : vec3(AO);
		out_Color.a = diffuse.a;
		out_Glow = vec4(0.0, 0.0, 0.0, out_Color.a);
		return;
	}
  #endif
#else
	lightColor = var_Color.rgb;
  #if defined(USE_LIGHTMAP)
	lightColor *= lightmapColor.rgb;
  #endif
  #if defined(USE_SSGI)
	vec3 vertexDynamicLight = CalcDynamicLightContribution(var_Position, var_Normal);
	lightColor += vertexDynamicLight;
	g_ssgiDynamicDiffuse = diffuse.rgb * vertexDynamicLight;
	SSGIWriteReceiver(normalize(var_Normal), 1.0, diffuse.rgb);
  #else
	lightColor += CalcDynamicLightContribution(var_Position, var_Normal);
  #endif

    out_Color.rgb = diffuse.rgb * lightColor;
#endif

	out_Color.a = diffuse.a;
#if defined(USE_SSGI)
	vec3 ssgiLitColor = out_Color.rgb;
	vec3 ssgiEmissive = vec3(0.0);
#endif
	vec3 emissive = vec3(0.0);
	if (abs(u_EmissiveParams.w) == 1.0)
	{
#if defined(USE_SILHOUETTE_POM)
		vec3 emissiveLinear = textureGrad(u_EmissiveMap, texCoords, g_pomGradX, g_pomGradY).rgb * u_EmissiveParams.rgb;
#else
		vec3 emissiveLinear = texture(u_EmissiveMap, texCoords).rgb * u_EmissiveParams.rgb;
#endif
#if defined(USE_SSGI)
		ssgiEmissive = emissiveLinear;
#endif
		if (u_EmissiveParams.w > 0.0)
		{
			emissive = emissiveLinear;
			out_Color.rgb += emissiveLinear;
		}
		else
		{
			emissive = EmissiveLinearToLegacyScene(emissiveLinear);
			out_Color.rgb = EmissiveLinearToLegacyScene(
				EmissiveLegacySceneToLinear(out_Color.rgb) + emissiveLinear);
		}
	}
	else if (abs(u_EmissiveParams.w) == 2.0)
	{
		emissive = out_Color.rgb;
	}
	// Legacy glow still exports the complete stage color. New emissive stages
	// export only their masked emission when the legacy keyword is absent.
	out_Glow = mix(vec4(emissive, out_Color.a), out_Color, u_EnableTextures.x);
#if defined(USE_SSGI)
	SSGIWriteRadiance(ssgiLitColor, ssgiEmissive, out_Color.rgb);
#endif
#if defined(USE_SKIN_SSS_BUFFER) && defined(PER_PIXEL_LIGHTING)
	// skin diffuse for the screen-space diffusion (tr_skinsss.cpp); the
	// visible (sharp) color keeps it too, the composite swaps it
	if (g_skinScatter > 0.0)
	{
		out_SkinDiffuse = vec4(g_skinDiffuse * g_skinScatter, max(dot(-viewDir, normalize(u_ViewForward)), 1e-3));
  #if !defined(USE_SSGI) && !(defined(USE_SSR) && defined(USE_SPECULARMAP))
		// normal aware diffusion: nobody else wrote the normal (receiver 0)
		out_SSRNormal = vec4(SSREncodeNormal(N), roughness, 0.0);
  #endif
	}
#endif
}
