/*[Vertex]*/
in vec3 attr_Position;
in vec3 attr_Normal;

#if defined(USE_VERTEX_ANIMATION)
in vec3 attr_Position2;
in vec3 attr_Normal2;
#elif defined(USE_SKELETAL_ANIMATION)
in uvec4 attr_BoneIndexes;
in vec4 attr_BoneWeights;
#endif

in vec4 attr_Color;
in vec2 attr_TexCoord0;

#if defined(USE_TCGEN)
in vec2 attr_TexCoord1;
in vec2 attr_TexCoord2;
in vec2 attr_TexCoord3;
in vec2 attr_TexCoord4;
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

#if defined(USE_DEFORM_VERTEXES) || defined(USE_RGBAGEN)
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

#if defined(USE_SKELETAL_ANIMATION)
layout(std140) uniform Bones
{
	mat3x4 u_BoneMatrices[MAX_G2_BONES];
};
#endif

uniform vec4 u_DiffuseTexMatrix;
uniform vec4 u_DiffuseTexOffTurb;

#if defined(USE_TCGEN)
uniform int u_TCGen0;
uniform vec3 u_TCGen0Vector0;
uniform vec3 u_TCGen0Vector1;
#endif

uniform vec4 u_BaseColor;
uniform vec4 u_VertColor;

#if defined(USE_RGBAGEN)
uniform int u_ColorGen;
uniform int u_AlphaGen;
#endif

#if defined(USE_RGBAGEN) || defined(USE_DEFORM_VERTEXES)
uniform vec4 u_Disintegration; // origin, threshhold
#endif

#if defined(USE_FLARE_TEST)
uniform sampler2D u_ScreenDepthMap;
uniform vec4 u_LightOrigin;
#endif

out vec2 var_DiffuseTex;
out vec4 var_Color;
out float var_LeafFlutter;	// r_leafFlutterDebug 8: displacement magnitude
#if defined(USE_FOG)
out vec3 var_WSPosition;
#endif

#if defined(USE_DEFORM_VERTEXES)
float GetNoiseValue( float x, float y, float z, float t )
{
	// Variation on the 'one-liner random function'.
	// Not sure if this is still 'correctly' random
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
#if defined(USE_RGBAGEN)
	if (u_ColorGen == CGEN_DISINTEGRATION_2)
	{
		vec3 delta = u_Disintegration.xyz - pos;
		float sqrDistance = dot(delta, delta);
		vec3 normalScale = vec3(-0.01);
		if ( sqrDistance < u_Disintegration.w )
		{
			normalScale = vec3(2.0, 2.0, 0.5);
		}
		else if ( sqrDistance < u_Disintegration.w + 50 )
		{
			normalScale = vec3(1.0, 1.0, 0.0);
		}
		return pos + normal * normalScale;
	}
#endif
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

		case DEFORM_PROJECTION_SHADOW:
		{
			vec3 ground = u_DeformParams0.xyz;
			float groundDist = u_DeformParams0.w;
			vec3 lightDir = u_DeformParams1.xyz;

			float d = dot( lightDir, ground );

			lightDir = lightDir * max( 0.5 - d, 0.0 ) + ground;
			d = 1.0 / dot( lightDir, ground );

			vec3 lightPos = lightDir * d;

			return pos - lightPos * dot( pos, ground ) + groundDist;
		}
		//case DEFORM_DISINTEGRATION:
		//{
		//}
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

#if defined(USE_TCGEN)
vec2 GenTexCoords(int TCGen, vec3 position, vec3 normal, vec3 TCGenVector0, vec3 TCGenVector1)
{
	vec2 tex = attr_TexCoord0.st;

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

#if defined(USE_RGBAGEN)
vec4 CalcColor(vec3 position, vec3 normal)
{
	vec4 color = u_VertColor * attr_Color + u_BaseColor;

	if (u_ColorGen == CGEN_LIGHTING_DIFFUSE)
	{
		float incoming = clamp(dot(normal, u_ModelLightDir), 0.0, 1.0);

		color.rgb = clamp(u_DirectedLight * incoming + u_AmbientLight, 0.0, 1.0);
	}
	else if (u_ColorGen == CGEN_DISINTEGRATION_1)
	{
		vec3 delta = u_Disintegration.xyz - position;
		float sqrDistance = dot(delta, delta);
		if (sqrDistance < u_Disintegration.w)
		{
			color *= 0.0;
		}
		else if (sqrDistance < u_Disintegration.w + 60.0)
		{
			color *= vec4(0.0, 0.0, 0.0, 1.0);
		}
		else if (sqrDistance < u_Disintegration.w + 150.0)
		{
			color *= vec4(0.435295, 0.435295, 0.435295, 1.0);
		}
		else if (sqrDistance < u_Disintegration.w + 180.0)
		{
			color *= vec4(0.6862745, 0.6862745, 0.6862745, 1.0);
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

	vec3 localOrigin = (inverse(u_ModelMatrix) * vec4(u_ViewOrigin, 1.0)).xyz;
	vec3 viewer = localOrigin - position;

	if (u_AlphaGen == AGEN_LIGHTING_SPECULAR)
	{
		vec3 lightDir = normalize(vec3(-960.0, 1980.0, 96.0) - position);
		vec3 reflected = -reflect(lightDir, normal);

		color.a = clamp(dot(reflected, normalize(viewer)), 0.0, 1.0);
		color.a *= color.a;
		color.a *= color.a;
	}
	else if (u_AlphaGen == AGEN_LIGHTING_SPECULAR_STATIC)
	{
		vec3 reflected = -reflect(u_ModelLightDir, normal);

		color.a = clamp(dot(reflected, normalize(viewer)), 0.0, 1.0);
		color.a *= color.a;
		color.a *= color.a;
	}
	else if (u_AlphaGen == AGEN_PORTAL)
	{
		color.a = clamp(length(viewer) / u_PortalRange, 0.0, 1.0);
	}

	return color;
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
	vec3 position  = mix(attr_Position, attr_Position2, u_VertexLerp);
	vec3 normal    = mix(attr_Normal,   attr_Normal2,   u_VertexLerp);
	normal = normalize(normal - vec3(0.5));
#elif defined(USE_SKELETAL_ANIMATION)
	mat4x3 influence =
		GetBoneMatrix(attr_BoneIndexes[0]) * attr_BoneWeights[0] +
        GetBoneMatrix(attr_BoneIndexes[1]) * attr_BoneWeights[1] +
        GetBoneMatrix(attr_BoneIndexes[2]) * attr_BoneWeights[2] +
        GetBoneMatrix(attr_BoneIndexes[3]) * attr_BoneWeights[3];

    vec3 position = influence * vec4(attr_Position, 1.0);
    vec3 normal = normalize(influence * vec4(attr_Normal - vec3(0.5), 0.0));
#else
	vec3 position  = attr_Position;
	vec3 normal    = attr_Normal * 2.0 - vec3(1.0);
#endif

#if defined(USE_DEFORM_VERTEXES)
	position = DeformPosition(position, normal, attr_TexCoord0.st);
	normal = DeformNormal( position, normal );
#endif

	vec4 wsPosition = u_ModelMatrix * vec4(position, 1.0);

	// r_leafFlutter: FOLIAGE_LEAF surfaces only (leaf_flutter.glsl). Vertex
	// lit, so only the position moves.
	var_LeafFlutter = 0.0;
	if (LeafFlutterEnabled())
	{
		vec2 leafBands;
		vec3 leafOffset = LeafFlutterOffset(wsPosition.xyz, LeafFlutterSeed(u_ModelMatrix[3].xyz),
			u_LeafFlutterParams.x, LeafFlutterWeight(position), leafBands);
		wsPosition.xyz += leafOffset;
		var_LeafFlutter = LeafFlutterMagnitude(leafOffset);
	}

	// FOLIAGE_PLANT root bend: wind + character colliders (plant_bend.glsl)
	if (PlantBendEnabled())
	{
		float plantHeat;
		wsPosition.xyz = PlantBendPosition(wsPosition.xyz, PlantBendRoot(u_ModelMatrix),
			PlantBendWeight(position), u_PlantBendTime.x, false, plantHeat);
		var_LeafFlutter = plantHeat;
	}

	gl_Position = u_viewProjectionMatrix * wsPosition;

#if defined(USE_TCGEN)
	vec2 tex = GenTexCoords(u_TCGen0, position.xyz, normal, u_TCGen0Vector0, u_TCGen0Vector1);
#else
	vec2 tex = attr_TexCoord0.st;
#endif

#if defined(USE_TCMOD)
	var_DiffuseTex = ModTexCoords(tex, position, u_DiffuseTexMatrix, u_DiffuseTexOffTurb);
#else
    var_DiffuseTex = tex;
#endif

	if ( u_FXVolumetricBase >= 0.0 )
	{
		vec3 viewForward = u_ViewForward.xyz;
		float d = clamp(dot(normalize(viewForward), normalize(normal)), 0.0, 1.0);
		d = d * d;
		d = d * d;

		var_Color = vec4(u_FXVolumetricBase * (1.0 - d));
	}
	else
	{
#if defined(USE_RGBAGEN)
		var_Color = CalcColor(position.xyz, normal);
#else
		var_Color = u_VertColor * attr_Color + u_BaseColor;
#endif
	}
	
#if defined(USE_FLARE_TEST)
	vec4 samplePosition = u_viewProjectionMatrix * u_LightOrigin;
	vec3 depthPosition = samplePosition.xyz / samplePosition.w * 0.5 + 0.5;
	float depthSample = texture(u_ScreenDepthMap, depthPosition.xy).r;
	gl_Position *= float(depthPosition.z < depthSample);
#endif

#if defined(USE_FOG)
	var_WSPosition = wsPosition.xyz;
#endif
}


/*[Fragment]*/
#if defined(USE_FOG)
struct Fog
{
	vec4 plane;
	vec4 color;
	float depthToOpaque;
	bool hasPlane;
};

layout(std140) uniform Fogs
{
	int u_NumFogs;
	Fog u_Fogs[MAX_GPU_FOGS];
};

uniform int u_FogIndex;
uniform vec4 u_FogColorMask;

#if defined(USE_VOLUMETRIC_FOG)
uniform sampler3D u_VolumetricLightMap;

uniform vec3 u_LightGridOrigin;
uniform vec3 u_LightGridCellInverseSize;
#endif

layout(std140) uniform Camera
{
	mat4 u_viewProjectionMatrix;
	vec4 u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
};
#endif

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

uniform sampler2D u_DiffuseMap;
uniform sampler2D u_EmissiveMap;
#if defined(USE_ALPHA_TEST)
uniform int u_AlphaTestType;
#endif

// x = glow out, y = deluxe, z = screen shadow, w = cube
uniform vec4 u_EnableTextures;
// rgb = linear scale; |w| = 0 disabled, 1 explicit, 2 auto source; sign = legacy/linear scene
uniform vec4 u_EmissiveParams;
// r_autoPBRDebug (tr_autopbr.cpp): a = 1 marks a lit stage that is still
// vertex lit here instead of lightall, drawn as a flat rgb color
uniform vec4 u_MaterialDebug;
uniform float u_LeafFlutterDebug;	// r_leafFlutterDebug 8: color by var_LeafFlutter

in vec2 var_DiffuseTex;
in vec4 var_Color;
in float var_LeafFlutter;
#if defined(USE_FOG)
in vec3 var_WSPosition;
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


#if defined(USE_FOG)
#if defined(USE_VOLUMETRIC_FOG)
vec3 CalcVolumetricFogColor(in vec3 startPosition, in vec3 endPosition, in Fog fog)
{
	ivec3 gridSize = textureSize(u_VolumetricLightMap, 0);
	vec3 invGridSize = u_LightGridCellInverseSize / vec3(gridSize);
	
	const int steps = r_volumetricFogSamples;
	vec3 step = (endPosition - startPosition) / steps;
	float z = fog.depthToOpaque * length(step);

	vec3 position = startPosition;
	float transmittance  = 1.0;
	vec3 color = vec3(0.0);
	for (int i = 0; i < steps; i++)
	{
		float currentTransmittance = exp(-z);
		float currentOpacity = 1.0 - currentTransmittance;

		vec3 gridCell = (position - u_LightGridOrigin) * invGridSize;
		color += texture(u_VolumetricLightMap, gridCell).rgb * transmittance * currentOpacity;
		transmittance *= currentTransmittance;
		
		position += step;
	}
	return color;
}
#endif

vec4 CalcFog(in vec3 viewOrigin, in vec3 position, in Fog fog)
{
	bool inFog = dot(viewOrigin, fog.plane.xyz) - fog.plane.w >= 0.0 || !fog.hasPlane;

	// line: x = o + tv
	// plane: (x . n) + d = 0
	// intersects: dot(o + tv, n) + d = 0
	//             dot(o + tv, n) = -d
	//             dot(o, n) + t*dot(n, v) = -d
	//             t = -(d + dot(o, n)) / dot(n, v)
	vec3 V = position - viewOrigin;

	// fogPlane is inverted in tr_bsp for some reason.
	float t = -(fog.plane.w + dot(viewOrigin, -fog.plane.xyz)) / dot(V, -fog.plane.xyz);

	bool intersects = (t > 0.0 && t < 0.995);
	if (inFog == intersects)
		return vec4(0.0);

	float distToVertexFromViewOrigin = length(V);
	float distToIntersectionFromViewOrigin = t * distToVertexFromViewOrigin;

	float distOutsideFog = max(distToVertexFromViewOrigin - distToIntersectionFromViewOrigin, 0.0);
	float distThroughFog = mix(distOutsideFog, distToVertexFromViewOrigin, inFog);

	float z = fog.depthToOpaque * distThroughFog;
#if defined(USE_VOLUMETRIC_FOG)
	vec3 startPosition = mix((V * t) + viewOrigin, viewOrigin, vec3(inFog));
	vec3 endPosition = (normalize(V) * distThroughFog) + startPosition;
	vec3 color = CalcVolumetricFogColor(startPosition, endPosition, fog);
	return vec4(color * fog.color.rgb, 1.0 - clamp(exp(-z), 0.0, 1.0));
#else
	return vec4(fog.color.rgb, 1.0 - clamp(exp(-(z * z)), 0.0, 1.0));
#endif
}
#endif

void main()
{
	vec4 color  = texture(u_DiffuseMap, var_DiffuseTex);
	color *= var_Color;
	vec3 emissive = vec3(0.0);
#if defined(USE_ALPHA_TEST)
	if (u_AlphaTestType == ALPHA_TEST_GT0)
	{
		if (color.a == 0.0)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_LT128)
	{
		if (color.a >= 0.5)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE128)
	{
		if (color.a < 0.5)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE192)
	{
		if (color.a < 0.75)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_E255)
	{
		if (color.a < 1.00)
			discard;
	}
#endif

	if (abs(u_EmissiveParams.w) == 1.0)
	{
		vec3 emissiveLinear = texture(u_EmissiveMap, var_DiffuseTex).rgb * u_EmissiveParams.rgb;
		if (u_EmissiveParams.w > 0.0)
		{
			emissive = emissiveLinear;
			color.rgb += emissiveLinear;
		}
		else
		{
			// Legacy maps store display-encoded scene values. Decode the reflected
			// result so the actual addition still happens in linear HDR.
			emissive = EmissiveLinearToLegacyScene(emissiveLinear);
			color.rgb = EmissiveLinearToLegacyScene(
				EmissiveLegacySceneToLinear(color.rgb) + emissiveLinear);
		}
	}

#if defined(USE_FOG) && defined(USE_FROXEL_FOG)
	// sprite particle lighting (r_particleLighting, RB_ParticleLightSetupDraw): the authored color of
	// smoke / dust is a reflectance, lit by the particle light field relative to the map average.
	// Before the fog: the fog between the sprite and the camera (S, T) is applied once, below.
	if (u_ParticleLight.w >= 2.0)
	{
		// r_particleLightingDebug 5: magenta = lit, cyan = unlit (additive) sprite
		vec3 tint = (u_ParticleLight.w >= 3.0) ? vec3(0.0, 1.0, 1.0) : vec3(1.0, 0.0, 1.0);
		color.rgb = tint * max(dot(color.rgb, vec3(0.3333)), 0.1);
	}
	else if (u_ParticleLight.w > 0.0)
	{
		vec3 gain = ParticleLightLookup(var_WSPosition) * u_ParticleLight.x;
		gain = clamp(gain, vec3(u_ParticleLight.y), vec3(u_ParticleLight.z));
		color.rgb *= mix(vec3(1.0), gain, u_ParticleLight.w);
	}
#endif

#if defined(USE_FOG)
#if defined(USE_FROXEL_FOG)
	// froxel volume of the main view (r_volumetricFog 2), or no fog here when
	// the composite after the opaque layers applies it
	vec4 fogColorOpacity = vec4(0.0);
#if defined(USE_FROXEL_RGB)
	// RGB extinction (r_volumetricFogRGBExtinction): the opacity per channel, 1 - T.rgb
	vec3 fogOpacityRGB = vec3(0.0);
	if (u_FroxelFogMode == 3)
	{
		vec3 froxelT;
		fogColorOpacity.rgb = FroxelFogRGB(var_WSPosition, froxelT);
		fogOpacityRGB = vec3(1.0) - froxelT;
	}
	else
#endif
	if (u_FroxelFogMode == 1)
	{
		vec4 froxelFog = FroxelFog(var_WSPosition);
		fogColorOpacity = vec4(froxelFog.rgb, 1.0 - froxelFog.a);
	}
	else if (u_FroxelFogMode == 0)
	{
		Fog fog = u_Fogs[u_FogIndex];
		fogColorOpacity = CalcFog(u_ViewOrigin, var_WSPosition, fog);
	}
#else
	Fog fog = u_Fogs[u_FogIndex];
	vec4 fogColorOpacity = CalcFog(u_ViewOrigin, var_WSPosition, fog);
#endif
#if defined(USE_LIQUID_FOG_BLENDS) && defined(USE_FROXEL_FOG)
	// r_volumetricWater (tr_liquid.cpp RB_LiquidFogBlendMask): the froxel fog
	// of the blends the legacy masks get wrong or leave out
	//   mask (0 0 0 -n)  filter: color -> mix(n, color, T), alpha *= T
	//   mask (1 1 1 1)   premultiplied (ONE, ONE_MINUS_SRC_ALPHA): color * T + S * alpha
	bool liquidBlendFog = false;
	if (u_FroxelFogMode == 1 || u_FroxelFogMode == 3)
	{
		vec3 fogT = vec3(1.0 - fogColorOpacity.a);
#if defined(USE_FROXEL_RGB)
		if (u_FroxelFogMode == 3)
			fogT = vec3(1.0) - fogOpacityRGB;
#endif
		if (u_FogColorMask.a < 0.0)
		{
			color.rgb = mix(vec3(-u_FogColorMask.a), color.rgb, fogT);
			color.a *= dot(fogT, vec3(1.0 / 3.0));
			emissive *= fogT;
			liquidBlendFog = true;
		}
		else if (all(greaterThan(u_FogColorMask, vec4(0.5))))
		{
			color.rgb = color.rgb * fogT + fogColorOpacity.rgb * color.a;
			emissive *= fogT;
			liquidBlendFog = true;
		}
	}
	if (!liquidBlendFog)
	{
#endif
#if defined(USE_VOLUMETRIC_FOG)
#if defined(USE_FROXEL_RGB)
	if (u_FroxelFogMode == 3)
	{
		// the same with the opacity per channel
		color.rgb *= vec3(1.0) - u_FogColorMask.a * fogOpacityRGB;
		color.rgb += u_FogColorMask.a * fogColorOpacity.rgb;
		color.rgb *= vec3(1.0) - u_FogColorMask.rgb * fogOpacityRGB;
		emissive *= vec3(1.0) - u_FogColorMask.a * fogOpacityRGB;
		emissive *= vec3(1.0) - u_FogColorMask.rgb * fogOpacityRGB;
	}
	else
	{
#endif
	color.rgb *= 1.0 - u_FogColorMask.a * fogColorOpacity.a;
	color.rgb += u_FogColorMask.a * fogColorOpacity.rgb;
	color.rgb *= vec3(1.0) - u_FogColorMask.rgb * fogColorOpacity.a;
	emissive *= 1.0 - u_FogColorMask.a * fogColorOpacity.a;
	emissive *= vec3(1.0) - u_FogColorMask.rgb * fogColorOpacity.a;
#if defined(USE_FROXEL_RGB)
	}
#endif
#else
#if defined(USE_FROXEL_RGB)
	// without the volumetric fog blend (not with r_volumetricFog 2): the mean opacity
	if (u_FroxelFogMode == 3)
		fogColorOpacity.a = dot(fogOpacityRGB, vec3(1.0 / 3.0));
#endif
	color *= vec4(1.0) - u_FogColorMask * fogColorOpacity.a;
	emissive *= vec3(1.0) - u_FogColorMask.rgb * fogColorOpacity.a;
#endif
#if defined(USE_LIQUID_FOG_BLENDS) && defined(USE_FROXEL_FOG)
	}
#endif
#endif

	if (abs(u_EmissiveParams.w) == 2.0)
		emissive = color.rgb;

	if (u_MaterialDebug.a > 0.0)
	{
		// keep the vertex lighting as shading so the shape stays readable
		float shade = clamp(dot(var_Color.rgb, vec3(0.3333)), 0.0, 1.0);
		vec3 debugColor = u_MaterialDebug.rgb;
		if (u_LeafFlutterDebug == 8.0)
			debugColor = mix(vec3(0.05, 0.1, 0.8), vec3(1.0, 0.85, 0.1), var_LeafFlutter);
		out_Color = vec4(debugColor * (0.35 + 0.65 * shade), color.a);
		out_Glow = vec4(0.0, 0.0, 0.0, color.a);
		return;
	}

	out_Color = color;
	out_Glow = mix(vec4(emissive, color.a), color, u_EnableTextures.x);
}
