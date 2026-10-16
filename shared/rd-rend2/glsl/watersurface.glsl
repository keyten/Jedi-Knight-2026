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
	vec2 u_ScenePad0;
	vec4 u_AOParams;
	vec4 u_AOParams2;
	vec4 u_PBRParams;
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

#if defined(USE_WATER_MOTION_REPROJECTION)
layout(std140) uniform PreviousEntity
{
	mat4 u_PreviousModelMatrix;
	vec4 u_PreviousLocalLightOrigin;
	vec3 u_PreviousAmbientLight;
	float u_PreviousEntityTime;
	vec3 u_PreviousDirectedLight;
	float u_PreviousFXVolumetricBase;
	vec3 u_PreviousModelLightDir;
	float u_PreviousVertexLerp;
};

layout(std140) uniform TemporalInfo
{
	mat4 u_previousViewProjectionMatrix;
	vec2 u_currentJitter;
	vec2 u_previousJitter;
	float u_previousFrameTime;
};
#endif

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
out vec2 var_GeometrySlope;
out float var_GeometrySkirt;
out vec3 var_ShoreData;
uniform vec4 u_Water[WATER_UNIFORM_VEC4S];
uniform sampler2D u_WaterInteractionMap;
#if defined(USE_WATER_MOTION_REPROJECTION)
uniform vec4 u_WaterMotion[WATER_MOTION_VEC4S];
uniform sampler2D u_WaterPrevInteractionMap;
uniform mat4 u_SSRReproject; // exact water-history world-to-previous-clip matrix
out vec4 var_PreviousClip;
out vec3 var_PreviousPosition;
out vec3 var_CurrentAnalytic;
out vec3 var_PreviousAnalytic;
out vec2 var_InteractionMotion;
#endif
uniform sampler2D u_WaterFlowField;	// r_waterFlowField: velocity / body speed, shore distance, impingement
uniform vec4 u_WaterFlowParams[2];	// see the fragment stage

// r_waterFlowImpact: water running into a wall stands up against it by the
// stagnation head U^2 / 2g and sloshes; the field's impingement is how
// directly and how close the stream meets the wall.
float WaterWallPile(vec3 worldPosition, float time)
{
	vec2 uv = (worldPosition.xy - u_Water[WATER_U_BODY_MAPPING].xy) * u_Water[WATER_U_BODY_MAPPING].zw;
	float impingement = textureLod(u_WaterFlowField, uv, 0.0).a;
	float slosh = 0.7 + 0.3 * sin(time * 3.3 + dot(worldPosition.xy, vec2(0.071, 0.053))) *
		sin(time * 1.9 + dot(worldPosition.xy, vec2(-0.041, 0.083)) + 1.3);
	return impingement * u_WaterFlowParams[0].w * slosh;
}

vec3 WaterShoreData(float encodedDistance)
{
	if (u_Water[WATER_U_SHORELINE].x < 0.5 || u_Water[WATER_U_GEOMETRY_FOAM].y < 0.5)
		return vec3(65504.0, 1.0, 0.0);
	float distance = max(abs(encodedDistance) - 0.25, 0.0);
	float rigid = step(encodedDistance, 0.0);
	float width = max(u_Water[WATER_U_SHORELINE].y, 1.0);
	float profileShore = clamp(u_Water[WATER_U_BODY_BOUNDS].w, 0.0, 1.5);
	float rigidEdge = clamp(0.03 + profileShore * 0.033333, 0.03, 0.08);
	float naturalEdge = clamp(0.15 + profileShore * 0.25, 0.15, 0.40);
	float rigidAttenuation = mix(rigidEdge, 1.0, smoothstep(0.0, width, distance));
	float naturalAttenuation = mix(naturalEdge, 1.0, smoothstep(0.0, width * 0.35, distance));
	return vec3(distance, mix(naturalAttenuation, rigidAttenuation, rigid), rigid);
}

float WaterInteractionHeight(vec3 worldPosition)
{
	if (u_Water[WATER_U_BODY_MAPPING + 1].z < 0.5) return 0.0;
	vec2 uv = (worldPosition.xy - u_Water[WATER_U_BODY_MAPPING].xy) * u_Water[WATER_U_BODY_MAPPING].zw;
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 0.0;
	vec4 field = texture(u_WaterInteractionMap, u_Water[WATER_U_BODY_MAPPING + 3].xy + uv * u_Water[WATER_U_BODY_MAPPING + 3].zw);
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
	vec4 oldTerms[8];
	vec4 commonOmega[2];
	vec4 oldOmega[2];
	for (int commonIndex = 0; commonIndex < 8; ++commonIndex) commonTerms[commonIndex] = u_Water[WATER_U_TARGET_TERMS + commonIndex];
	for (int oldIndex = 0; oldIndex < 8; ++oldIndex) oldTerms[oldIndex] = u_Water[WATER_U_OLD_TERMS + oldIndex];
	commonOmega[0] = u_Water[WATER_U_TARGET_OMEGA]; commonOmega[1] = u_Water[WATER_U_TARGET_OMEGA + 1];
	oldOmega[0] = u_Water[WATER_U_OLD_OMEGA]; oldOmega[1] = u_Water[WATER_U_OLD_OMEGA + 1];
	WaterSurfaceCommonState commonState = EvaluateWaterSurfaceCommon(worldPosition, time, u_Water[WATER_U_WAVE_CONTROL], u_Water[WATER_U_BODY_WAVES],
		vec4(u_Water[WATER_U_WAVE_QUALITY].x, u_Water[WATER_U_WAVE_QUALITY].y, u_Water[WATER_U_BODY_FLOW_DEPTH].z, u_Water[WATER_U_WAVE_DEBUG].z), u_Water[WATER_U_FLOW],
		vec4(u_Water[WATER_U_FLOW + 1].xy, u_Water[WATER_U_WAVE_QUALITY].w, u_Water[WATER_U_FLOW + 1].w), commonTerms, commonOmega, 1.0, u_Water[WATER_U_GEOMETRY].x);
	float windBlend = clamp(u_Water[WATER_U_WIND + 2].x, 0.0, 1.0);
	if (windBlend < 0.999)
	{
		WaterSurfaceCommonState oldState = EvaluateWaterSurfaceCommon(worldPosition, time, u_Water[WATER_U_WAVE_CONTROL], u_Water[WATER_U_BODY_WAVES],
			vec4(u_Water[WATER_U_WAVE_QUALITY].x, u_Water[WATER_U_WAVE_QUALITY].y, u_Water[WATER_U_BODY_FLOW_DEPTH].z, u_Water[WATER_U_WAVE_DEBUG].z), u_Water[WATER_U_FLOW],
			vec4(u_Water[WATER_U_FLOW + 1].xy, u_Water[WATER_U_WAVE_QUALITY].w, u_Water[WATER_U_FLOW + 1].w), oldTerms, oldOmega, 1.0, u_Water[WATER_U_GEOMETRY].x);
		commonState.displacement = mix(oldState.displacement, commonState.displacement, windBlend);
		commonState.slope = mix(oldState.slope, commonState.slope, windBlend);
		commonState.velocity = mix(oldState.velocity, commonState.velocity, windBlend);
		commonState.height = mix(oldState.height, commonState.height, windBlend);
		commonState.attenuation = mix(oldState.attenuation, commonState.attenuation, windBlend);
		commonState.curvature = mix(oldState.curvature, commonState.curvature, windBlend);
	}
	w.displacement = commonState.displacement; w.slope = commonState.slope; w.velocity = commonState.velocity;
	w.height = commonState.height; w.attenuation = commonState.attenuation; w.curvature = commonState.curvature;
	return w;
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

vec3 DeformPositionAt(const vec3 pos, const vec3 normal, const vec2 st,
	float entityTime, float shaderFrameTime, float shaderOffset)
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

			float scale = CalculateDeformScale( WF_SIN, entityTime + shaderFrameTime + shaderOffset, bulgeWidth * st.x, bulgeSpeed );

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
			float scale = CalculateDeformScale( u_DeformFunc, entityTime + shaderFrameTime + shaderOffset, phase + offset, frequency );

			return pos + normal * (base + scale * amplitude);
		}

		case DEFORM_MOVE:
		{
			float base = u_DeformParams0.x;
			float amplitude = u_DeformParams0.y;
			float phase = u_DeformParams0.z;
			float frequency = u_DeformParams0.w;
			vec3 direction = u_DeformParams1.xyz;

			float scale = CalculateDeformScale( u_DeformFunc, entityTime + shaderFrameTime + shaderOffset, phase, frequency );

			return pos + direction * (base + scale * amplitude);
		}
	}
}

vec3 DeformPosition(const vec3 pos, const vec3 normal, const vec2 st)
{
	return DeformPositionAt(pos, normal, st, u_entityTime, u_frameTime, u_Time);
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
	vec3 fall = normalize(u_Water[WATER_U_WATERFALL + 1].xyz);
	vec3 sheetNormal = normalize(u_Water[WATER_U_WATERFALL + 2].xyz);
	vec3 across = normalize(cross(sheetNormal, fall));
	float along = clamp((dot(worldPosition, fall) - u_Water[WATER_U_WATERFALL + 3].x) * u_Water[WATER_U_WATERFALL + 2].w, 0.0, 1.0);
	float side = dot(worldPosition, across);
	float advected = dot(worldPosition, fall) - time * u_Water[WATER_U_WATERFALL + 1].w;
	float breakup = u_Water[WATER_U_WATERFALL + 3].y;
	float sheetWave = sin(side * 0.0107 + advected * 0.0027) +
		0.55 * sin(side * -0.023 + advected * 0.0061 + 1.7);
	float medium = sin(side * 0.061 + advected * 0.018 + 0.7) * (0.25 + 0.75 * along);
	// Keep the crest as one coherent sheet, then let displacement grow through
	// the turbulent middle and broken lower regime.  `along` is the resolved
	// body-local flow coordinate, never world Z.
	float regimeBreakup = mix(0.16, 1.0, smoothstep(0.18, 0.86, along));
	return sheetNormal * (breakup * regimeBreakup * (2.8 * sheetWave + 1.35 * medium));
}

#if defined(USE_WATER_MOTION_REPROJECTION)
WaterWaveState EvaluatePreviousWaterSurface(vec3 worldPosition)
{
	WaterWaveState w;
	w.displacement = vec3(0.0); w.slope = vec2(0.0); w.velocity = vec3(0.0);
	w.height = 0.0; w.curvature = 0.0; w.attenuation = 1.0;
	vec4 targetTerms[8], oldTerms[8], targetOmega[2], oldOmega[2];
	for (int i = 0; i < 8; ++i)
	{
		targetTerms[i] = u_WaterMotion[WATER_MOTION_TARGET_TERMS + i];
		oldTerms[i] = u_WaterMotion[WATER_MOTION_OLD_TERMS + i];
	}
	targetOmega[0] = u_WaterMotion[WATER_MOTION_TARGET_OMEGA];
	targetOmega[1] = u_WaterMotion[WATER_MOTION_TARGET_OMEGA + 1];
	oldOmega[0] = u_WaterMotion[WATER_MOTION_OLD_OMEGA];
	oldOmega[1] = u_WaterMotion[WATER_MOTION_OLD_OMEGA + 1];
	vec4 quality = u_WaterMotion[WATER_MOTION_WAVE_QUALITY];
	WaterSurfaceCommonState state = EvaluateWaterSurfaceCommon(worldPosition,
		u_WaterMotion[WATER_MOTION_FRAME].x, u_WaterMotion[WATER_MOTION_WAVE_CONTROL],
		u_WaterMotion[WATER_MOTION_BODY_WAVES], quality,
		u_WaterMotion[WATER_MOTION_FLOW], u_WaterMotion[WATER_MOTION_FLOW_DETAIL],
		targetTerms, targetOmega, 1.0, u_WaterMotion[WATER_MOTION_WIND_GEOMETRY].y);
	float blend = clamp(u_WaterMotion[WATER_MOTION_WIND_GEOMETRY].x, 0.0, 1.0);
	if (blend < 0.999)
	{
		WaterSurfaceCommonState oldState = EvaluateWaterSurfaceCommon(worldPosition,
			u_WaterMotion[WATER_MOTION_FRAME].x, u_WaterMotion[WATER_MOTION_WAVE_CONTROL],
			u_WaterMotion[WATER_MOTION_BODY_WAVES], quality,
			u_WaterMotion[WATER_MOTION_FLOW], u_WaterMotion[WATER_MOTION_FLOW_DETAIL],
			oldTerms, oldOmega, 1.0, u_WaterMotion[WATER_MOTION_WIND_GEOMETRY].y);
		state.displacement = mix(oldState.displacement, state.displacement, blend);
		state.slope = mix(oldState.slope, state.slope, blend);
		state.height = mix(oldState.height, state.height, blend);
	}
	w.displacement = state.displacement; w.slope = state.slope; w.height = state.height;
	return w;
}

vec3 PreviousWaterShoreData(float encodedDistance)
{
	vec4 shore = u_WaterMotion[WATER_MOTION_SHORE];
	if (shore.x < 0.5 || shore.w < 0.5) return vec3(65504.0, 1.0, 0.0);
	float distance = max(abs(encodedDistance) - 0.25, 0.0);
	float rigid = step(encodedDistance, 0.0);
	float width = max(shore.y, 1.0);
	float profileShore = clamp(shore.z, 0.0, 1.5);
	float rigidEdge = clamp(0.03 + profileShore * 0.033333, 0.03, 0.08);
	float naturalEdge = clamp(0.15 + profileShore * 0.25, 0.15, 0.40);
	return vec3(distance,
		mix(mix(naturalEdge, 1.0, smoothstep(0.0, width * 0.35, distance)),
			mix(rigidEdge, 1.0, smoothstep(0.0, width, distance)), rigid), rigid);
}

float PreviousWaterInteractionHeight(vec3 worldPosition)
{
	if (u_WaterMotion[WATER_MOTION_INTERACTION].z < 0.5 ||
		u_WaterMotion[WATER_MOTION_FRAME].z < 0.5) return 0.0;
	vec2 uv = (worldPosition.xy - u_WaterMotion[WATER_MOTION_BODY_MAPPING].xy) *
		u_WaterMotion[WATER_MOTION_BODY_MAPPING].zw;
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 0.0;
	vec4 rect = u_WaterMotion[WATER_MOTION_INTERACTION_RECT];
	vec4 field = texture(u_WaterPrevInteractionMap, rect.xy + uv * rect.zw);
	return field.a > 0.5 ? field.r : 0.0;
}

float PreviousWaterWallPile(vec3 worldPosition)
{
	vec4 impact = u_WaterMotion[WATER_MOTION_FLOW_IMPACT];
	if (impact.z < 0.5) return 0.0;
	vec2 uv = (worldPosition.xy - u_WaterMotion[WATER_MOTION_BODY_MAPPING].xy) *
		u_WaterMotion[WATER_MOTION_BODY_MAPPING].zw;
	float impingement = textureLod(u_WaterFlowField, uv, 0.0).a;
	float time = u_WaterMotion[WATER_MOTION_FRAME].x;
	float slosh = 0.7 + 0.3 * sin(time * 3.3 + dot(worldPosition.xy, vec2(0.071, 0.053))) *
		sin(time * 1.9 + dot(worldPosition.xy, vec2(-0.041, 0.083)) + 1.3);
	return impingement * impact.w * slosh;
}

vec3 EvaluatePreviousWaterfallSheet(vec3 worldPosition)
{
	vec4 wf1 = u_WaterMotion[WATER_MOTION_WATERFALL_1];
	vec4 wf2 = u_WaterMotion[WATER_MOTION_WATERFALL_2];
	vec4 wf3 = u_WaterMotion[WATER_MOTION_WATERFALL_3];
	vec3 fall = normalize(wf1.xyz), sheetNormal = normalize(wf2.xyz);
	vec3 across = normalize(cross(sheetNormal, fall));
	float along = clamp((dot(worldPosition, fall) - wf3.x) * wf2.w, 0.0, 1.0);
	float side = dot(worldPosition, across);
	float advected = dot(worldPosition, fall) - u_WaterMotion[WATER_MOTION_FRAME].x * wf1.w;
	float sheetWave = sin(side * 0.0107 + advected * 0.0027) +
		0.55 * sin(side * -0.023 + advected * 0.0061 + 1.7);
	float medium = sin(side * 0.061 + advected * 0.018 + 0.7) * (0.25 + 0.75 * along);
	return sheetNormal * (wf3.y * mix(0.16, 1.0, smoothstep(0.18, 0.86, along)) *
		(2.8 * sheetWave + 1.35 * medium));
}
#endif

void main()
{
	vec3 position = attr_Position;
	vec3 normal   = attr_Normal * 2.0 - vec3(1.0);
#if defined(USE_WATER_MOTION_REPROJECTION)
	vec3 previousObjectPosition = attr_Position;
#endif

#if defined(USE_DEFORM_VERTEXES)
	position = DeformPosition(position, normal, attr_TexCoord0.st);
#if defined(USE_WATER_MOTION_REPROJECTION)
	previousObjectPosition = DeformPositionAt(previousObjectPosition, normal, attr_TexCoord0.st,
		u_PreviousEntityTime, u_previousFrameTime, u_WaterMotion[WATER_MOTION_WIND_GEOMETRY].w);
#endif
	normal = DeformNormal(position, normal);
#endif

	vec4 wsPosition = u_ModelMatrix * vec4(position, 1.0);
	var_BasePosition = wsPosition.xyz;
	var_GeometryDisplacement = vec3(0.0);
	var_GeometrySlope = vec2(0.0);
	vec3 currentAnalytic = vec3(0.0);
	float currentInteractionMotion = 0.0;
	bool waterfallGeometry = u_Water[WATER_U_WATERFALL].x > 0.5;
	float currentSkirt = waterfallGeometry ||
		(u_Water[WATER_U_GEOMETRY_FOAM].z > 2.5 && u_Water[WATER_U_GEOMETRY_FOAM].z < 3.5) ? 1.0 : attr_TexCoord1.x;
	var_GeometrySkirt = waterfallGeometry ? 0.0 : 1.0 - attr_TexCoord1.x;
	var_ShoreData = waterfallGeometry ? vec3(65000.0, 1.0, 0.0) : WaterShoreData(attr_TexCoord1.y);
	if (u_Water[WATER_U_GEOMETRY_FOAM].y > 0.5)
	{
		WaterWaveState geometryWave = EvaluateWaterSurface(var_BasePosition, u_Water[WATER_U_FRAME].z);
		if (waterfallGeometry)
			geometryWave.displacement = EvaluateWaterfallSheet(var_BasePosition, u_Water[WATER_U_FRAME].z);
		else
		{
			geometryWave.displacement *= var_ShoreData.y;
			geometryWave.slope *= var_ShoreData.y;
		}
		currentAnalytic = geometryWave.displacement;
		float profileShore = clamp(u_Water[WATER_U_BODY_BOUNDS].w, 0.0, 1.5);
		// Local disturbances reflect at walls; only the ambient macro waves are
		// suppressed near the boundary, so the field keeps most of its height.
		float interactionEdge = mix(clamp(0.60 + profileShore * 0.10, 0.60, 0.75),
			clamp(0.45 + profileShore * 0.05, 0.45, 0.55), var_ShoreData.z);
		if (u_Water[WATER_U_WATERFALL].x < 0.5)
		{
			currentInteractionMotion = WaterInteractionHeight(var_BasePosition) * max(var_ShoreData.y, interactionEdge);
			geometryWave.displacement.z += currentInteractionMotion;
		}
		if (!waterfallGeometry && u_WaterFlowParams[0].z > 0.5)
			geometryWave.displacement.z += WaterWallPile(var_BasePosition, u_Water[WATER_U_FRAME].z);
		var_GeometryDisplacement = geometryWave.displacement * currentSkirt;
		var_GeometrySlope = waterfallGeometry ? vec2(0.0) : geometryWave.slope * attr_TexCoord1.x;
		wsPosition.xyz += var_GeometryDisplacement;
	}

#if defined(USE_WATER_MOTION_REPROJECTION)
	vec4 previousWsPosition = u_PreviousModelMatrix * vec4(previousObjectPosition, 1.0);
	vec3 previousBasePosition = previousWsPosition.xyz;
	vec3 previousAnalytic = vec3(0.0);
	float previousInteractionMotion = 0.0;
	if (u_WaterMotion[WATER_MOTION_SHORE].w > 0.5)
	{
		bool previousWaterfall = u_WaterMotion[WATER_MOTION_WATERFALL].x > 0.5;
		vec3 previousShore = previousWaterfall ? vec3(65000.0, 1.0, 0.0) :
			PreviousWaterShoreData(attr_TexCoord1.y);
		WaterWaveState previousWave = EvaluatePreviousWaterSurface(previousBasePosition);
		if (previousWaterfall)
			previousWave.displacement = EvaluatePreviousWaterfallSheet(previousBasePosition);
		else
			previousWave.displacement *= previousShore.y;
		previousAnalytic = previousWave.displacement;
		float profileShore = clamp(u_WaterMotion[WATER_MOTION_SHORE].z, 0.0, 1.5);
		float interactionEdge = mix(clamp(0.60 + profileShore * 0.10, 0.60, 0.75),
			clamp(0.45 + profileShore * 0.05, 0.45, 0.55), previousShore.z);
		if (!previousWaterfall)
		{
			previousInteractionMotion = PreviousWaterInteractionHeight(previousBasePosition) *
				max(previousShore.y, interactionEdge);
			previousWave.displacement.z += previousInteractionMotion;
			previousWave.displacement.z += PreviousWaterWallPile(previousBasePosition);
		}
		previousWsPosition.xyz += previousWave.displacement *
			(u_WaterMotion[WATER_MOTION_WIND_GEOMETRY].z > 0.5 ? 1.0 : attr_TexCoord1.x);
	}
	var_PreviousPosition = previousWsPosition.xyz;
	// Use the transform stored with the water SSR images, not an independently
	// advancing camera matrix.  Both sides now name the same rendered frame.
	var_PreviousClip = u_SSRReproject * previousWsPosition;
	float previousSkirt = u_WaterMotion[WATER_MOTION_WIND_GEOMETRY].z > 0.5 ? 1.0 : attr_TexCoord1.x;
	var_CurrentAnalytic = currentAnalytic * currentSkirt;
	var_PreviousAnalytic = previousAnalytic * previousSkirt;
	var_InteractionMotion = vec2(currentInteractionMotion * currentSkirt,
		previousInteractionMotion * previousSkirt);
#endif
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
// result encoded again (u_Water[WATER_U_FRAME].w).
//
// u_Water: see RB_WaterSurfaceSetupDraw
//   [WATER_U_SURFACE] IOR, roughness (perceptual), normal strength, refraction scale
//   [WATER_U_OPTICS] reflection scale, SSR scale, absorption scale, path length scale
//   [WATER_U_EXTINCTION] extinction per unit rgb, froxel volume holds this liquid
//   [WATER_U_SCATTER] single scattering albedo rgb, phase g
//   [WATER_U_PROJECTION] P[0], P[5], P[8], P[9]
//   [WATER_U_FRAME] P[14], P[10], time (s), linear scene
//   [WATER_U_DEBUG] debug view, split x, flags, longest path
//   [WATER_U_VIEW_RIGHT/VIEW_UP] world view basis and froxel fade
//   [WATER_U_ENVIRONMENT] fallback environment radiance rgb, sun specular
//   [WATER_U_DETAIL/DRIFT] detail scale and wrapped world-wave drift
//   [WATER_U_WAVE_CONTROL/QUALITY] analytic-wave controls and quality
//   [WATER_U_BODY_WAVES] body amplitude, wavelength, speed, micro strength
//   [WATER_U_BODY_BOUNDS/BODY_FLOW_DEPTH] bounds, flow, mean depth and profile
//   [WATER_U_WAVE_DEBUG] debug, body ID, choppiness, wave multiplier
//   [WATER_U_GEOMETRY_FOAM] legacy deform, geometry controls, foam multiplier
//   [WATER_U_TARGET_TERMS..+7] target analytic wave direction.xy, amplitude, wave number
//   [WATER_U_FLOW] resolved world flow velocity xyz, enabled
//   [WATER_U_FLOW+1] resolved speed, detail-advection scale, flow debug, packed source/confidence
//   [WATER_U_BODY_MAPPING..+3] body UV mapping, interaction state/source and atlas rectangle
//   [WATER_U_INTERSECTION_FOAM] intersection foam enabled, world width, strength, debug mask
//   [WATER_U_FOAM] persistent foam enabled, debug mode, texel world size xy
//   [WATER_U_WHITEWATER] whitewater enabled, optical strength, foam injection, debug mode
//   [WATER_U_WATERFALL] waterfall enabled, quality, debug, mean sheet thickness
//   [WATER_U_WATERFALL+1..+4] direction, sheet geometry, breakup/aeration and profile optics
//   [WATER_U_OLD_TERMS..+7] old analytic spectrum during a wind crossfade
//   [WATER_U_WIND..+2] resolved/current/target wind and transition state
//   [WATER_U_GEOMETRY] resolved geometry-wave count, measured max top edge, 4x edge threshold, reserved
//   [WATER_U_TARGET_OMEGA..+1] target temporal frequencies (components 0..7)
//   [WATER_U_OLD_OMEGA..+1] old temporal frequencies for wind crossfade
//
// USE_WATER_SNELL (r_waterSnell 1, a permutation: without it the prompt-1 program is unchanged): seen from inside the liquid, the surface is
// the water -> air interface (eta = ior): Snell's window is the refraction of the scene above through it
// and total internal reflection beyond the critical angle comes out of the exact Fresnel (F = 1, nothing
// transmitted), averaged over the unresolved wave slopes; no window mask. The reflected light under the
// surface is the scene in the liquid: SSR -> a cubemap captured in the liquid -> the liquid itself,
// through the medium along the reflected path.

uniform vec4 u_Water[WATER_UNIFORM_VEC4S];
uniform vec4 u_WaterPass; // x: reflection prepass, y: history / resolved reflection valid, z: history weight
#if defined(USE_WATER_MOTION_REPROJECTION)
uniform vec4 u_WaterMotion[WATER_MOTION_VEC4S];
#endif
uniform vec4 u_WaterOptics2; // x: opt-in rough dielectric transmission / eta transport
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
	vec2 u_ScenePad0;
	vec4 u_AOParams;
	vec4 u_AOParams2;
	vec4 u_PBRParams;
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
	vec4 physical;
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
uniform sampler2D u_WaterInteractionMap; // R height, GB world slope, A zero=dry / 1+foam=wet
uniform sampler2D u_WaterRainMap;        // global classified-water rain height/velocity field
uniform mat4 u_WeatherMvp;
uniform vec4 u_WaterRainParams;          // explicit slope, aggregate micro, coverage, debug
uniform sampler2D u_WaterFoamMap;        // R concentration, or RGBA sources in debug 3
uniform sampler2D u_WaterFoamPattern;    // tiling foam structure: R membranes, G bubbles, B patches, A coarse membranes
uniform sampler2D u_WaterFlowField;      // r_waterFlowField: velocity / body speed, shore distance, impingement
// [0] flow field enabled, body flow speed (u/s), wall impact enabled, stagnation head (world units)
// [1] two-phase period (s), foam pattern world scale, flow debug, unused
uniform vec4 u_WaterFlowParams[2];
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
in vec2 var_GeometrySlope;
in float var_GeometrySkirt;
in vec3 var_ShoreData;
in vec3 var_Position;
in vec3 var_Normal;
in vec2 var_FlowTex;
#if defined(USE_WATER_MOTION_REPROJECTION)
in vec4 var_PreviousClip;
in vec3 var_PreviousPosition;
in vec3 var_CurrentAnalytic;
in vec3 var_PreviousAnalytic;
in vec2 var_InteractionMotion;
#endif

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
	vec4 oldTerms[8];
	vec4 commonOmega[2];
	vec4 oldOmega[2];
	for (int commonIndex = 0; commonIndex < 8; ++commonIndex) commonTerms[commonIndex] = u_Water[WATER_U_TARGET_TERMS + commonIndex];
	for (int oldIndex = 0; oldIndex < 8; ++oldIndex) oldTerms[oldIndex] = u_Water[WATER_U_OLD_TERMS + oldIndex];
	commonOmega[0] = u_Water[WATER_U_TARGET_OMEGA]; commonOmega[1] = u_Water[WATER_U_TARGET_OMEGA + 1];
	oldOmega[0] = u_Water[WATER_U_OLD_OMEGA]; oldOmega[1] = u_Water[WATER_U_OLD_OMEGA + 1];
	WaterSurfaceCommonState commonState = EvaluateWaterSurfaceCommon(worldPosition, time, u_Water[WATER_U_WAVE_CONTROL], u_Water[WATER_U_BODY_WAVES],
		vec4(u_Water[WATER_U_WAVE_QUALITY].x, u_Water[WATER_U_WAVE_QUALITY].y, u_Water[WATER_U_BODY_FLOW_DEPTH].z, u_Water[WATER_U_WAVE_DEBUG].z), u_Water[WATER_U_FLOW],
		vec4(u_Water[WATER_U_FLOW + 1].xy, u_Water[WATER_U_WAVE_QUALITY].w, u_Water[WATER_U_FLOW + 1].w), commonTerms, commonOmega, 2.0, u_Water[WATER_U_GEOMETRY].x);
	float windBlend = clamp(u_Water[WATER_U_WIND + 2].x, 0.0, 1.0);
	if (windBlend < 0.999)
	{
		WaterSurfaceCommonState oldState = EvaluateWaterSurfaceCommon(worldPosition, time, u_Water[WATER_U_WAVE_CONTROL], u_Water[WATER_U_BODY_WAVES],
			vec4(u_Water[WATER_U_WAVE_QUALITY].x, u_Water[WATER_U_WAVE_QUALITY].y, u_Water[WATER_U_BODY_FLOW_DEPTH].z, u_Water[WATER_U_WAVE_DEBUG].z), u_Water[WATER_U_FLOW],
			vec4(u_Water[WATER_U_FLOW + 1].xy, u_Water[WATER_U_WAVE_QUALITY].w, u_Water[WATER_U_FLOW + 1].w), oldTerms, oldOmega, 2.0, u_Water[WATER_U_GEOMETRY].x);
		commonState.displacement = mix(oldState.displacement, commonState.displacement, windBlend);
		commonState.slope = mix(oldState.slope, commonState.slope, windBlend);
		commonState.velocity = mix(oldState.velocity, commonState.velocity, windBlend);
		commonState.height = mix(oldState.height, commonState.height, windBlend);
		commonState.attenuation = mix(oldState.attenuation, commonState.attenuation, windBlend);
		commonState.curvature = mix(oldState.curvature, commonState.curvature, windBlend);
	}
	w.displacement = commonState.displacement; w.slope = commonState.slope; w.velocity = commonState.velocity;
	w.height = commonState.height; w.attenuation = commonState.attenuation; w.curvature = commonState.curvature;
	return w;
}

vec3 SceneToLinear(vec3 c)
{
	if (u_Water[WATER_U_FRAME].w > 0.5)
		return c;
	c = max(c, vec3(0.0));
	vec3 lo = c * (1.0 / 12.92);
	vec3 hi = pow((c + vec3(0.055)) * (1.0 / 1.055), vec3(2.4));
	return mix(lo, hi, greaterThan(c, vec3(0.04045)));
}

vec3 LinearToScene(vec3 c)
{
	if (u_Water[WATER_U_FRAME].w > 0.5)
		return c;
	c = max(c, vec3(0.0));
	vec3 lo = 12.92 * c;
	vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
	return mix(lo, hi, greaterThanEqual(c, vec3(0.0031308)));
}

// linear view depth of a hardware depth value
float WaterLinearDepth(float d)
{
	return u_Water[WATER_U_FRAME].x / (d * 2.0 - 1.0 + u_Water[WATER_U_FRAME].y);
}

bool WaterIsSky(float d)
{
	return d >= 0.999999;
}

// world position of render target uv at view depth z
vec3 WaterWorldPosition(vec2 uv, float z)
{
	vec2 ndc = (uv - u_Water[WATER_U_VIEWPORT].xy) / u_Water[WATER_U_VIEWPORT].zw * 2.0 - 1.0;
	vec2 xy = (ndc + u_Water[WATER_U_PROJECTION].zw) * z / u_Water[WATER_U_PROJECTION].xy;
	return u_ViewOrigin + u_Water[WATER_U_VIEW_RIGHT].xyz * xy.x + u_Water[WATER_U_VIEW_UP].xyz * xy.y + normalize(u_ViewForward) * z;
}

vec2 WaterProject(vec3 p)
{
	vec4 clip = u_viewProjectionMatrix * vec4(p, 1.0);
	return (clip.xy / max(clip.w, 1e-4) * 0.5 + 0.5) * u_Water[WATER_U_VIEWPORT].zw + u_Water[WATER_U_VIEWPORT].xy;
}

// render target uv of the point L along the refracted ray Rt from P, the offset scaled by
// r_waterSurfaceRefraction and bounded
vec2 WaterRefractedUV(vec3 P, vec3 Rt, float L, vec2 uv)
{
	vec2 offset = (WaterProject(P + Rt * L) - uv) * u_Water[WATER_U_SURFACE].w;
	float offsetLength = length(offset);
	if (offsetLength > 0.12)
		offset *= 0.12 / offsetLength;
	vec2 halfTexel = 0.5 / vec2(textureSize(u_WaterSceneMap, 0));
	vec2 lo = u_Water[WATER_U_VIEWPORT].xy + halfTexel;
	vec2 hi = u_Water[WATER_U_VIEWPORT].xy + u_Water[WATER_U_VIEWPORT].zw - halfTexel;
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
	return c * (D_GGX(NH, alpha) * V_SmithGGXCorrelated(alpha, NV, NL) * F * NL);
}

// Rough dielectric transmission from an air-side sun to an underwater
// observer. The refractive half-vector, Jacobian and radiance-mode eta scale
// are evaluated together; this intentionally is not a post-hoc ior^2 tint.
vec3 WaterSunTransmission(vec3 N, vec3 V, vec3 L, vec3 c, float alpha, float ior)
{
	float NV = max(dot(N, V), 1e-4);
	float NL = max(-dot(N, L), 0.0);
	if (NL <= 0.0)
		return vec3(0.0);
	vec3 H = normalize(ior * V + L);
	if (dot(H, N) < 0.0) H = -H;
	float NH = clamp(dot(N, H), 0.0, 1.0);
	float VH = max(abs(dot(V, H)), 1e-4);
	float LH = max(abs(dot(L, H)), 1e-4);
	float F = FresnelDielectric(VH, 1.0 / ior);
	float denominator = max(abs(ior * VH - LH), 1e-3);
	float jacobian = (VH * LH) / (NV * NL * denominator * denominator);
	float transport = 1.0 / (ior * ior);
	float lobe = (1.0 - F) * D_GGX(NH, alpha) *
		V_SmithGGXCorrelated(alpha, NV, NL) * jacobian * transport * NL;
	return c * min(lobe, 32.0);
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
	return SceneToLinear(u_Water[WATER_U_ENVIRONMENT].rgb) * mix(0.55, 1.0, clamp(R.z * 0.5 + 0.5, 0.0, 1.0));
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
	vec3 right = u_Water[WATER_U_VIEW_RIGHT].xyz;
	vec3 up = u_Water[WATER_U_VIEW_UP].xyz;
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
    vec2 lo = floor(u_Water[WATER_U_VIEWPORT].xy * fullSize / scale);
    vec2 hi = ceil((u_Water[WATER_U_VIEWPORT].xy + u_Water[WATER_U_VIEWPORT].zw) * fullSize / scale);
    vec2 local = (sceneUV - u_Water[WATER_U_VIEWPORT].xy) / u_Water[WATER_U_VIEWPORT].zw;
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
#if defined(USE_WATER_MOTION_REPROJECTION)
	bool receiverHistoryValid = u_WaterMotion[WATER_MOTION_FRAME].y > 0.5;
	if (u_WaterPass.y > 0.5 && hit.w > 0.0 && receiverHistoryValid)
	{
		vec4 prevClip = var_PreviousClip;
#else
	if (u_WaterPass.y > 0.5 && hit.w > 0.0)
	{
		vec4 prevClip = u_SSRReproject * vec4(P, 1.0);
#endif
		vec2 prevUV = (prevClip.xy / max(prevClip.w, 1e-4) * 0.5 + 0.5) * u_Water[WATER_U_VIEWPORT].zw + u_Water[WATER_U_VIEWPORT].xy;
		if (prevClip.w > 0.0 && SSRInsideView(prevUV))
		{
            prevUV = WaterReflectionUV(prevUV);
			vec4 geom = texture(u_SSRHistoryGeomMap, prevUV);
			vec4 oldHit = texture(u_SSRPrevHitMap, prevUV);
			vec4 old = texture(u_SSRHistoryMap, prevUV);
			float tolerance = max(0.01 * z, 0.5);
			float cone = max(2.0, distance * SSRConeTangent(roughness));
#if defined(USE_WATER_MOTION_REPROJECTION)
			vec3 previousHitWorld = var_PreviousPosition + oldHit.xyz;
			vec3 currentHitWorld = P + hit.xyz;
			if (geom.x > 0.0 && abs(geom.x - prevClip.w) < tolerance &&
				dot(SSRDecodeNormal(geom.yz), N) > 0.97 && abs(geom.w - roughness) < 0.05 &&
				oldHit.w > 0.0 && length(previousHitWorld - currentHitWorld) < cone)
#else
			if (geom.x > 0.0 && abs(geom.x - prevClip.w) < tolerance &&
				dot(SSRDecodeNormal(geom.yz), N) > 0.97 && abs(geom.w - roughness) < 0.05 &&
				oldHit.w > 0.0 && length(oldHit.xyz - hit.xyz) < cone)
#endif
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

#if defined(USE_WATER_MOTION_REPROJECTION)
	int motionDebug = int(u_WaterMotion[WATER_MOTION_FRAME].w + 0.5);
	if (motionDebug > 0)
	{
		vec2 previousUV = var_PreviousClip.xy / max(var_PreviousClip.w, 1e-4) * 0.5 + 0.5;
		vec2 currentUV = uv;
		if (motionDebug == 1) current = vec4(receiverHistoryValid ? vec3(0.1, 1.0, 0.2) : vec3(1.0, 0.1, 0.1), 1.0);
		else if (motionDebug == 2) current = vec4(fract(abs(P) * 0.03125), 1.0);
		else if (motionDebug == 3) current = vec4(fract(abs(var_PreviousPosition) * 0.03125), 1.0);
		else if (motionDebug == 4) current = vec4(0.5 + (previousUV - currentUV) * 16.0, length(previousUV - currentUV) * 32.0, 1.0);
		else if (motionDebug == 5) current = vec4(abs(var_CurrentAnalytic - var_PreviousAnalytic) * 0.25, 1.0);
		else if (motionDebug == 6) current = vec4(abs(var_InteractionMotion.x - var_InteractionMotion.y), var_InteractionMotion * 0.1 + 0.5, 1.0);
		else if (motionDebug == 7) current = vec4(abs((P - var_PreviousPosition) - (var_GeometryDisplacement - var_PreviousAnalytic)) * 0.1, 1.0);
		else if (motionDebug == 8) current = vec4(previousUV, length(previousUV - currentUV) * 32.0, 1.0);
		else if (motionDebug == 9) current = vec4(u_WaterMotion[WATER_MOTION_INTERACTION].w / 11.0, receiverHistoryValid ? 1.0 : 0.0, 0.0, 1.0);
		else
		{
			if (u_WaterPass.y <= 0.5)
			{
				current = vec4(1.0, 0.0, 0.0, 1.0);
				out_Color = current;
				out_Glow = vec4(z, SSREncodeNormal(N), roughness);
				out_SSRNormal = hit;
				return;
			}
			vec4 cameraClip = u_SSRReproject * vec4(P, 1.0);
			vec2 cameraUV = cameraClip.xy / max(cameraClip.w, 1e-4) * 0.5 + 0.5;
			current = gl_FragCoord.x < 0.5 * float(textureSize(u_SSRHistoryMap, 0).x) ?
				vec4(cameraUV, 0.0, 1.0) : vec4(previousUV, 1.0, 1.0);
		}
	}
#endif
	out_Color = current;
	out_Glow = vec4(z, SSREncodeNormal(N), roughness);
	out_SSRNormal = hit;
}

#endif

vec4 WaterRainSample(vec3 worldPosition, out vec2 worldSlope)
{
	worldSlope = vec2(0.0);
	if (u_WaterRainParams.x <= 0.0) return vec4(0.0);
	vec2 weatherUV = (u_WeatherMvp * vec4(worldPosition, 1.0)).xy * 0.5 + 0.5;
	vec2 uv = (weatherUV - 0.5) / max(u_WaterRainParams.z, 0.125) + 0.5;
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);
	// Two wrapped parcels make body-resolved stream flow continuous while the
	// global GL3.2 field remains camera/map space and needs no per-drop readback.
	float phase = fract(u_Water[WATER_U_FRAME].z * 0.5);
	vec2 extent = vec2(textureSize(u_WaterRainMap, 0));
	vec2 weatherWorld = max(vec2(1.0), 2.0 / vec2(length(u_WeatherMvp[0].xy), length(u_WeatherMvp[1].xy)));
	vec2 drift = u_Water[WATER_U_FLOW].xy * 2.0 / weatherWorld / max(u_WaterRainParams.z, 0.125);
	vec2 uv0 = uv - drift * phase;
	vec2 uv1 = uv - drift * (phase - 1.0);
	vec4 center = mix(texture(u_WaterRainMap, uv0), texture(u_WaterRainMap, uv1), phase);
	vec2 duv = 1.0 / extent;
	float l = mix(texture(u_WaterRainMap, uv0 - vec2(duv.x, 0.0)).r,
		texture(u_WaterRainMap, uv1 - vec2(duv.x, 0.0)).r, phase);
	float r = mix(texture(u_WaterRainMap, uv0 + vec2(duv.x, 0.0)).r,
		texture(u_WaterRainMap, uv1 + vec2(duv.x, 0.0)).r, phase);
	float d = mix(texture(u_WaterRainMap, uv0 - vec2(0.0, duv.y)).r,
		texture(u_WaterRainMap, uv1 - vec2(0.0, duv.y)).r, phase);
	float u = mix(texture(u_WaterRainMap, uv0 + vec2(0.0, duv.y)).r,
		texture(u_WaterRainMap, uv1 + vec2(0.0, duv.y)).r, phase);
	vec2 worldTexel = weatherWorld * max(u_WaterRainParams.z, 0.125) / extent;
	worldSlope = vec2((r - l) / (2.0 * worldTexel.x), (u - d) / (2.0 * worldTexel.y));
	return center;
}


vec4 WaterInteractionSample(vec3 worldPosition, out vec2 worldSlope)
{
	worldSlope = vec2(0.0);
	if (u_Water[WATER_U_BODY_MAPPING + 1].z < 0.5) return vec4(0.0);
	vec2 uv = (worldPosition.xy - u_Water[WATER_U_BODY_MAPPING].xy) * u_Water[WATER_U_BODY_MAPPING].zw;
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);
	vec2 atlasUV = u_Water[WATER_U_BODY_MAPPING + 3].xy + uv * u_Water[WATER_U_BODY_MAPPING + 3].zw;
	vec4 center = texture(u_WaterInteractionMap, atlasUV);
	float wet = step(0.5, center.a);
	worldSlope = center.gb * wet;
	// Preserve the public/debug field convention (height, slope X, foam, mask)
	// while the atlas keeps both slope components in GB.
	return vec4(center.r * wet, center.g * wet, max(center.a - 1.0, 0.0) * wet, wet);
}

// Depth discontinuities alone are not contacts: require an opaque sample in a
// narrow world-space slab around the water and a locally coherent surface
// whose normal crosses the water plane. This rejects shallow floors, distant
// silhouettes and the usual screen-space halo around every object.
float WaterIntersectionFoam(vec3 P, vec3 Ng, vec3 Tw, vec3 Bw, vec2 uv)
{
	if (u_Water[WATER_U_INTERSECTION_FOAM].x < 0.5 || u_WaterPass.x > 0.5)
		return 0.0;
	float width = max(u_Water[WATER_U_INTERSECTION_FOAM].y, 0.5);
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
		vec2 lo = u_Water[WATER_U_VIEWPORT].xy + texel * 1.5;
		vec2 hi = u_Water[WATER_U_VIEWPORT].xy + u_Water[WATER_U_VIEWPORT].zw - texel * 1.5;
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
	float length = 1.0 / u_Water[WATER_U_VIEW_UP].w;
	float t = clamp((z - u_Water[WATER_U_VIEW_RIGHT].w) / length, 0.0, 1.0);
	return length * (t * t * t - 0.5 * t * t * t * t) + max(z - u_Water[WATER_U_VIEW_RIGHT].w - length, 0.0);
}

float WaterMissingMedium(float z0, float z1)
{
	if (abs(z1 - z0) < 1e-3)
	{
		float t = clamp((z0 - u_Water[WATER_U_VIEW_RIGHT].w) * u_Water[WATER_U_VIEW_UP].w, 0.0, 1.0);
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

// Foam structure: a weighted sum of the procedural pattern, thresholded by
// concentration.  Dense foam is solid, thinning foam opens into membranes,
// lace and finally specks (the coverage approach of shipped ocean renderers)
// instead of fading as a translucent white veil.
float WaterFoamStructure(vec4 pattern)
{
	// patches break the regular membrane net into clumps and streaks
	return 0.32 * pattern.r + 0.18 * pattern.g + 0.5 * pattern.b * (0.6 + 0.4 * pattern.a);
}

float WaterFoamCoverage(float structure, float concentration, float sharpness)
{
	return clamp((structure + concentration * 1.35 - 1.0) * sharpness + 0.5 * step(0.002, concentration) *
		concentration * (1.0 - min(sharpness / 8.0, 1.0)), 0.0, 1.0);
}

vec2 WaterRotateFlow0(vec2 v) { return vec2(0.996 * v.x - 0.087 * v.y, 0.087 * v.x + 0.996 * v.y) * 0.82; }
vec2 WaterRotateFlow1(vec2 v) { return vec2(0.994 * v.x + 0.105 * v.y, -0.105 * v.x + 0.994 * v.y) * 1.19; }

void main()
{
	g_flags = int(u_Water[WATER_U_DEBUG].z + 0.5);
	int debugView = int(u_Water[WATER_U_DEBUG].x + 0.5);

	// split view: the legacy stages are drawn on the left (scissor), the water program on the right
	if (u_WaterPass.x < 0.5 && u_Water[WATER_U_DEBUG].y >= 0.0 && gl_FragCoord.x < u_Water[WATER_U_DEBUG].y)
		discard;

	vec2 uv = u_WaterPass.x > 0.5 ? WaterProject(var_Position) :
		gl_FragCoord.xy / vec2(textureSize(u_WaterSceneMap, 0));
	vec4 sceneHere = texture(u_WaterSceneMap, uv);

	float ior = u_Water[WATER_U_SURFACE].x;
	vec3 P = var_Position;
	vec3 toCamera = u_ViewOrigin - P;
	float cameraDistance = max(length(toCamera), 1e-3);
	vec3 V = toCamera / cameraDistance;
	vec3 Ng = normalize(var_Normal);
	bool waterfall = u_Water[WATER_U_WATERFALL].x > 0.5;
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
	vec3 Tw = waterfall ? normalize(cross(Ng, normalize(u_Water[WATER_U_WATERFALL + 1].xyz))) :
		(abs(Ng.z) > 0.7 ? normalize(vec3(1.0, 0.0, 0.0) - Ng * Ng.x) : normalize(cross(vec3(0.0, 0.0, 1.0), Ng)));
	vec3 Bw = waterfall ? normalize(u_Water[WATER_U_WATERFALL + 1].xyz) : normalize(cross(Ng, Tw));
	vec2 planar = vec2(dot(P, Tw), dot(P, Bw));
	float invSize = u_Water[WATER_U_DETAIL].y;
	vec2 flowPlanar = vec2(0.0);
	if (u_Water[WATER_U_FLOW].w > 0.5)
	{
		vec2 baseFlow = vec2(dot(u_Water[WATER_U_FLOW].xyz, Tw), dot(u_Water[WATER_U_FLOW].xyz, Bw)) *
			u_Water[WATER_U_FRAME].z * u_Water[WATER_U_FLOW + 1].y;
		// Two close but distinct directions/speeds avoid a single conveyor belt.
		flowPlanar = baseFlow;
	}
	vec2 flow0 = WaterRotateFlow0(flowPlanar);
	vec2 flow1 = WaterRotateFlow1(flowPlanar);
	float waterfallDetailLod = waterfall ? u_Water[WATER_U_WATERFALL + 5].y : 1.0;
	// r_waterFlowField: the local velocity varies over the body (slow at the
	// banks, turning along walls), so a linear scroll cannot be used; the
	// bands are advected with two half-period-offset phases and cross-faded
	// (flow-map advection), with a per-texel phase jitter against pulsing.
	vec2 bodyUV = (var_BasePosition.xy - u_Water[WATER_U_BODY_MAPPING].xy) * u_Water[WATER_U_BODY_MAPPING].zw;
	bool flowField = !waterfall && u_WaterFlowParams[0].x > 0.5;
	vec4 flowSample = flowField ? texture(u_WaterFlowField, bodyUV) : vec4(0.0);
	vec2 flowWorld = flowSample.rg * u_WaterFlowParams[0].y;
	vec2 flowOffset0 = vec2(0.0), flowOffset1 = vec2(0.0);
	float flowWeight0 = 1.0;
	if (flowField)
	{
		vec2 v = vec2(dot(vec3(flowWorld, 0.0), Tw), dot(vec3(flowWorld, 0.0), Bw)) * u_Water[WATER_U_FLOW + 1].y;
		float period = u_WaterFlowParams[1].x;
		float jitter = texture(u_WaterFoamPattern, var_BasePosition.xy * (1.0 / 1024.0)).b;
		float phase0 = fract(u_Water[WATER_U_FRAME].z / period + jitter);
		float phase1 = fract(phase0 + 0.5);
		flowWeight0 = 1.0 - abs(2.0 * phase0 - 1.0);
		flowOffset0 = v * period * (phase0 - 0.5);
		flowOffset1 = v * period * (phase1 - 0.5);
	}
	vec4 l0, l1, l2;
	if (flowField)
	{
		float w1 = 1.0 - flowWeight0;
		l0 = flowWeight0 * texture(u_WaterNormalMap, (planar - WaterRotateFlow0(flowOffset0)) * invSize + u_Water[WATER_U_DRIFT].xy) +
			w1 * texture(u_WaterNormalMap, (planar - WaterRotateFlow0(flowOffset1)) * invSize + u_Water[WATER_U_DRIFT].xy + 0.37);
		l1 = flowWeight0 * texture(u_WaterNormalMap, (planar - WaterRotateFlow1(flowOffset0)) * (invSize * 2.37) + u_Water[WATER_U_DRIFT].zw) +
			w1 * texture(u_WaterNormalMap, (planar - WaterRotateFlow1(flowOffset1)) * (invSize * 2.37) + u_Water[WATER_U_DRIFT].zw + 0.61);
		// the legacy stage scroll is one uniform vector too: replace it by a third
		// advected world band
		l2 = flowWeight0 * texture(u_WaterNormalMap, (planar - flowOffset0) * (invSize * 0.55) + 0.19) +
			w1 * texture(u_WaterNormalMap, (planar - flowOffset1) * (invSize * 0.55) + 0.83);
	}
	else
	{
		l0 = texture(u_WaterNormalMap, (planar - flow0) * invSize + u_Water[WATER_U_DRIFT].xy);
		// Low: coherent world-space band only. Medium: retain the authored legacy
		// detail as the second band. High: add the extra procedural octave.
		l1 = (!waterfall || waterfallDetailLod > 0.85) ?
			texture(u_WaterNormalMap, (planar - flow1) * (invSize * 2.37) + u_Water[WATER_U_DRIFT].zw) : l0;
		l2 = (!waterfall || waterfallDetailLod > 0.34) ?
			texture(u_WaterNormalMap, var_FlowTex * u_Water[WATER_U_DETAIL].x) : vec4(0.0);
	}
	float oldStrength = u_Water[WATER_U_SURFACE].z * 0.18;
	float strength = oldStrength * (u_Water[WATER_U_WAVE_CONTROL].x > 0.5 ? u_Water[WATER_U_WAVE_QUALITY].z * u_Water[WATER_U_BODY_WAVES].w : 1.0);
	vec2 slope = (l0.xy + 0.6 * l1.xy + 0.5 * l2.xy) * strength;
	vec3 microNormal = normalize(Ng - slope.x * Tw - slope.y * Bw);
	WaterWaveState waves = EvaluateWaterSurface(var_BasePosition, u_Water[WATER_U_FRAME].z);
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
	vec3 macroGradient = vec3(waves.slope + var_GeometrySlope, 0.0);
	vec2 macroSlope = vec2(dot(macroGradient, Tw), dot(macroGradient, Bw));
	vec2 interactionWorldSlope;
	vec4 interactionField = WaterInteractionSample(var_BasePosition, interactionWorldSlope);
	vec2 rainWorldSlope;
	vec4 rainField = WaterRainSample(var_BasePosition, rainWorldSlope);
	// The persistent field is coarse (tens of units per texel); warp its lookup
	// by about two texels of low-frequency noise so iso-lines of a bilinear
	// patch never show the grid as square foam edges.
	vec2 foamWarp = texture(u_WaterFoamPattern, var_BasePosition.xy * (1.0 / 384.0)).ba - 0.5;
	vec2 foamUV = bodyUV + foamWarp * 4.0 * u_Water[WATER_U_FOAM].zw * u_Water[WATER_U_BODY_MAPPING].zw;
	vec4 foamSample = texture(u_WaterFoamMap, clamp(foamUV, vec2(0.0), vec2(1.0)));
	// Foam pattern coordinates travel with the water: the same two phases as
	// the normal bands under a flow field, a plain scroll under uniform flow.
	vec2 foamWorldFlow = u_Water[WATER_U_FLOW].w > 0.5 && !waterfall ?
		u_Water[WATER_U_FLOW].xy * u_Water[WATER_U_FRAME].z : vec2(0.0);
	float foamScale = u_WaterFlowParams[1].y > 0.0 ? u_WaterFlowParams[1].y : 1.0 / 96.0;
	vec2 foamUV0, foamUV1;
	if (flowField)
	{
		float period = u_WaterFlowParams[1].x;
		float jitter = texture(u_WaterFoamPattern, var_BasePosition.xy * (1.0 / 1024.0)).b;
		float phase0 = fract(u_Water[WATER_U_FRAME].z / period + jitter);
		foamUV0 = (var_BasePosition.xy - flowWorld * period * (phase0 - 0.5)) * foamScale;
		foamUV1 = (var_BasePosition.xy - flowWorld * period * (fract(phase0 + 0.5) - 0.5)) * foamScale + 0.5;
	}
	else
	{
		foamUV0 = (var_BasePosition.xy - foamWorldFlow) * foamScale;
		// still water: a slow independent drift keeps the bubbles alive
		foamUV1 = foamUV0 * 1.13 + vec2(0.013, -0.009) * u_Water[WATER_U_FRAME].z + 0.5;
	}
	float foamStructure0 = WaterFoamStructure(texture(u_WaterFoamPattern, foamUV0));
	float foamStructure1 = WaterFoamStructure(texture(u_WaterFoamPattern, foamUV1));
	float foamSharp0 = clamp(0.35 / max(fwidth(foamStructure0), 1e-3), 1.5, 8.0);
	float foamSharp1 = clamp(0.35 / max(fwidth(foamStructure1), 1e-3), 1.5, 8.0);
	float foamPhaseWeight = flowField ? flowWeight0 : 0.5;
	float persistentFoam = u_Water[WATER_U_FOAM].x > 0.5 ? clamp(foamSample.r * u_Water[WATER_U_GEOMETRY_FOAM].w, 0.0, 1.0) : 0.0;
	float whitewater = u_Water[WATER_U_WHITEWATER].x > 0.5 ? clamp(foamSample.b * u_Water[WATER_U_WHITEWATER].y, 0.0, 1.0) : 0.0;
	float waterfallAlong = waterfall ? clamp((dot(var_BasePosition, normalize(u_Water[WATER_U_WATERFALL + 1].xyz)) - u_Water[WATER_U_WATERFALL + 3].x) * u_Water[WATER_U_WATERFALL + 2].w, 0.0, 1.0) : 0.0;
	float waterfallMiddle = waterfall ? smoothstep(0.16, 0.48, waterfallAlong) *
		(1.0 - smoothstep(0.66, 0.86, waterfallAlong)) : 0.0;
	float waterfallBottom = waterfall ? smoothstep(0.56, 0.90, waterfallAlong) : 0.0;
	float waterfallNoise = waterfall ? clamp(0.5 + 0.32 * (l0.x - l1.y) +
		0.18 * l2.x, 0.0, 1.0) : 0.0;
	float waterfallTurbulence = waterfall ? waterfallNoise *
		mix(0.16, 1.0, smoothstep(0.14, 0.88, waterfallAlong)) : 0.0;
	float waterfallThickness = waterfall ? u_Water[WATER_U_WATERFALL].w * mix(1.0, 0.72, waterfallAlong) *
		mix(0.78, 1.22, waterfallTurbulence) : 0.0;
	float waterfallAerationBreakup = mix(0.24, 1.0,
		smoothstep(0.28, 0.78, waterfallNoise));
	float waterfallAeration = waterfall ? clamp(u_Water[WATER_U_WATERFALL + 3].w *
		(0.24 * waterfallMiddle + 0.66 * waterfallBottom) *
		waterfallAerationBreakup + 0.10 * waterfallTurbulence, 0.0, 0.86) : 0.0;
	float waterfallFoamBreakup = mix(waterfallAeration,
		waterfallAeration * smoothstep(0.34, 0.72, waterfallNoise), u_Water[WATER_U_WATERFALL + 5].w);
	float waterfallFoamSource = waterfall ? clamp(waterfallFoamBreakup *
		(0.28 * waterfallMiddle + waterfallBottom), 0.0, 0.72) : 0.0;
	float waterfallSpraySource = waterfall ? waterfallFoamBreakup * u_Water[WATER_U_WATERFALL + 4].y *
		mix(0.45, 1.0, u_Water[WATER_U_WATERFALL + 5].w) : 0.0;
	float waterfallImpact = waterfall ? smoothstep(0.86, 1.0, waterfallAlong) * u_Water[WATER_U_WATERFALL + 4].z : 0.0;
	whitewater = max(whitewater, waterfallAeration);
	// The falling face uses a broken lit foam mask.  At the impact, the same
	// source continues into the existing persistent body-local foam field on
	// the receiving pool; no second transparent waterfall mesh is introduced.
	persistentFoam = max(persistentFoam, waterfallFoamSource);
	// horizontal water: structured coverage (waterfall sheets keep their
	// broken aeration mask, which already carries its own structure)
	if (!waterfall)
		persistentFoam = mix(WaterFoamCoverage(foamStructure1, persistentFoam, foamSharp1),
			WaterFoamCoverage(foamStructure0, persistentFoam, foamSharp0), foamPhaseWeight);
	vec3 interactionGradient = vec3(interactionWorldSlope, 0.0);
	float profileShore = clamp(u_Water[WATER_U_BODY_BOUNDS].w, 0.0, 1.5);
	// Local disturbances reflect at walls; only the ambient macro waves are
	// suppressed near the boundary, so the field keeps most of its height.
	float interactionEdge = mix(clamp(0.60 + profileShore * 0.10, 0.60, 0.75),
		clamp(0.45 + profileShore * 0.05, 0.45, 0.55), var_ShoreData.z);
	vec2 interactionSlope = vec2(dot(interactionGradient, Tw), dot(interactionGradient, Bw)) *
		max(var_ShoreData.y, interactionEdge);
	vec3 rainGradient = vec3(rainWorldSlope, 0.0);
	vec2 rainSlope = vec2(dot(rainGradient, Tw), dot(rainGradient, Bw)) *
		u_WaterRainParams.x * var_ShoreData.y;
	vec2 oldSlope = (l0.xy + 0.6 * l1.xy + 0.5 * l2.xy) * oldStrength;
	vec3 oldNormal = normalize(Ng - oldSlope.x * Tw - oldSlope.y * Bw);
	if (int(u_Water[WATER_U_WAVE_DEBUG].x + 0.5) == 8 && gl_FragCoord.x <
		(u_Water[WATER_U_VIEWPORT].x + u_Water[WATER_U_VIEWPORT].z * 0.5) * float(textureSize(u_WaterSceneMap, 0).x))
	{
		macroSlope = vec2(0.0);
		slope = oldSlope;
		strength = oldStrength;
	}
	// Aerated flow has unresolved, rapidly changing surface directions. Reuse
	// the procedural slope bands at different phases so it stays body/world
	// local and follows the resolved flow instead of scrolling in screen space.
	vec2 waterfallSlope = waterfall ? vec2((l0.x + 0.45 * l2.x) * u_Water[WATER_U_WATERFALL + 3].z,
		(l1.y - l0.y) * u_Water[WATER_U_WATERFALL + 3].z * (0.35 + 0.65 * waterfallAlong)) : vec2(0.0);
	vec2 whitewaterSlope = (l0.xy - l1.xy) * (0.38 * whitewater);
	vec2 wallSlope = vec2(0.0);
	float wallChop = 0.0;
	if (flowField && u_WaterFlowParams[0].z > 0.5)
	{
		// slope of the stagnation pile-up (finite difference of the field) and a
		// standing, non-advected short chop where the stream breaks on the wall
		vec2 texelUV = 6.0 * u_Water[WATER_U_BODY_MAPPING].zw;
		float pileX = texture(u_WaterFlowField, bodyUV + vec2(texelUV.x, 0.0)).a -
			texture(u_WaterFlowField, bodyUV - vec2(texelUV.x, 0.0)).a;
		float pileY = texture(u_WaterFlowField, bodyUV + vec2(0.0, texelUV.y)).a -
			texture(u_WaterFlowField, bodyUV - vec2(0.0, texelUV.y)).a;
		vec3 pileGradient = vec3(pileX, pileY, 0.0) * (u_WaterFlowParams[0].w / 12.0);
		wallChop = flowSample.a;
		vec4 chop0 = texture(u_WaterNormalMap, planar * (invSize * 3.1) + vec2(0.31, 0.17) * u_Water[WATER_U_FRAME].z);
		vec4 chop1 = texture(u_WaterNormalMap, planar * (invSize * 4.3) - vec2(0.23, -0.29) * u_Water[WATER_U_FRAME].z);
		wallSlope = vec2(dot(pileGradient, Tw), dot(pileGradient, Bw)) +
			(chop0.xy - chop1.xy) * (0.9 * wallChop * min(u_WaterFlowParams[0].w / 6.0, 1.5));
	}
	slope += macroSlope + interactionSlope + rainSlope + whitewaterSlope + waterfallSlope + wallSlope;
	vec2 variance = (max(l0.zw - l0.xy * l0.xy, vec2(0.0)) +
		0.36 * max(l1.zw - l1.xy * l1.xy, vec2(0.0)) +
		0.25 * max(l2.zw - l2.xy * l2.xy, vec2(0.0))) * (strength * strength);
	variance += vec2(0.08 * whitewater + 0.06 * wallChop);
	float rainActivity = clamp(rainField.b, 0.0, 1.0);
	variance += vec2(u_WaterRainParams.y * rainActivity *
		(0.018 + 0.012 * clamp(abs(rainField.g), 0.0, 4.0)));
	vec3 Nwater = normalize(Ng - slope.x * Tw - slope.y * Bw);
	vec3 N = inside ? -Nwater : Nwater;
	vec3 Nside = inside ? -Ng : Ng;	// geometric normal on the camera side
	float intersectionMask = !inside ? WaterIntersectionFoam(P, Ng, Tw, Bw, uv) : 0.0;
	float intersectionFoam = clamp(intersectionMask * u_Water[WATER_U_INTERSECTION_FOAM].z * u_Water[WATER_U_GEOMETRY_FOAM].w, 0.0, 1.0);
	int flowFieldDebug = int(u_WaterFlowParams[1].z + 0.5);
	if (flowFieldDebug >= 5 && u_WaterPass.x < 0.5)
	{
		vec3 debugColor = vec3(0.05, 0.0, 0.1);
		if (flowField)
		{
			if (flowFieldDebug == 5) debugColor = vec3(clamp(length(flowSample.rg), 0.0, 1.4) / 1.4) * vec3(0.3, 0.8, 1.0) +
				vec3(0.5 + 0.5 * normalize(flowSample.rg + 1e-5) * step(0.02, length(flowSample.rg)), 0.0) * 0.3;
			else if (flowFieldDebug == 6) debugColor = vec3(flowSample.a, 0.25 * flowSample.a, 0.0);
			else debugColor = vec3(flowSample.b);
		}
		out_Color = vec4(LinearToScene(debugColor), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}
	int foamDebug = int(u_Water[WATER_U_FOAM].y + 0.5);
	if (foamDebug > 0 && u_WaterPass.x < 0.5)
	{
		vec3 debugColor = foamDebug == 1 ? vec3(foamSample.r) :
			(foamDebug == 2 ? vec3(0.5 + 0.5 * normalize(vec3(u_Water[WATER_U_FLOW].xy, 0.001)).xy, 0.15) :
			(foamDebug == 4 ? vec3(persistentFoam) :
			(foamDebug == 5 ? vec3(fract(foamUV0), foamPhaseWeight) :
			clamp(foamSample.rgb + vec3(foamSample.a, 0.5 * foamSample.a, 0.0), 0.0, 1.0))));
		out_Color = vec4(LinearToScene(debugColor), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}
	int whitewaterDebug = int(u_Water[WATER_U_WHITEWATER].w + 0.5);
	if (whitewaterDebug > 0 && u_WaterPass.x < 0.5)
	{
		vec3 debugColor = vec3(0.0);
		if (whitewaterDebug == 1)
		{
			vec2 flow = length(u_Water[WATER_U_FLOW].xy) > 1e-5 ? normalize(u_Water[WATER_U_FLOW].xy) : vec2(0.0);
			debugColor = vec3(flow * 0.5 + 0.5, clamp(u_Water[WATER_U_FLOW + 1].x / 32.0, 0.0, 1.0));
		}
		else if (whitewaterDebug == 2) debugColor = vec3(foamSample.a, foamSample.a * 0.35, 0.0);
		else if (whitewaterDebug == 3) debugColor = vec3(whitewater);
		else debugColor = clamp(foamSample.rgb + vec3(foamSample.a, 0.5 * foamSample.a, 0.0), 0.0, 1.0);
		out_Color = vec4(LinearToScene(debugColor), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}
	if (u_Water[WATER_U_INTERSECTION_FOAM].w > 0.5 && u_WaterPass.x < 0.5)
	{
		out_Color = vec4(LinearToScene(vec3(intersectionMask, intersectionMask * 0.35, 0.0)), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}

	int interactionDebug = int(u_Water[WATER_U_BODY_MAPPING + 1].w + 0.5);
	if (interactionDebug > 0 && u_WaterPass.x < 0.5)
	{
		vec2 bodyUV = (var_BasePosition.xy - u_Water[WATER_U_BODY_MAPPING].xy) * u_Water[WATER_U_BODY_MAPPING].zw;
		vec3 debugColor = vec3(0.0);
		if (interactionDebug == 1) debugColor = interactionField.a > 0.5 ? vec3(bodyUV, 0.25) : vec3(0.15, 0.0, 0.0);
		else if (interactionDebug == 2) debugColor = vec3(interactionField.a);
		else if (interactionDebug == 3) debugColor = vec3(0.5 + interactionField.r / 16.0, 0.5 - abs(interactionField.r) / 16.0, 0.5 - interactionField.r / 16.0);
		else if (interactionDebug == 4) debugColor = vec3(0.5 + interactionWorldSlope * 4.0, 0.5);
		else if (interactionDebug == 5) debugColor = normalize(vec3(-interactionWorldSlope, 1.0)) * 0.5 + 0.5;
		else if (interactionDebug == 6)
		{
			float ring = abs(length((bodyUV - u_Water[WATER_U_BODY_MAPPING + 2].xy) / max(u_Water[WATER_U_BODY_MAPPING + 2].z, 1e-4)) - 1.0);
			debugColor = ring < 0.08 ? vec3(1.0, 0.1, 0.0) : vec3(0.05);
		}
		else if (interactionDebug == 7) debugColor = vec3(interactionField.b, 0.1 * interactionField.b, 0.0);
		else if (interactionDebug == 8) debugColor = interactionField.b > 0.0 ? vec3(0.1, 1.0, 0.2) : vec3(0.08, 0.12, 0.3);
		else if (interactionDebug == 9) debugColor = vec3(fract(bodyUV * 16.0), 0.25 + 0.75 * interactionField.a);
		out_Color = vec4(LinearToScene(clamp(debugColor, 0.0, 1.0)), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}
	int rainDebug = int(u_WaterRainParams.w + 0.5);
	if (rainDebug > 0 && u_WaterPass.x < 0.5)
	{
		vec3 debugColor = rainDebug == 1 ? vec3(0.0, 0.25 + 0.75 * step(0.001, u_WaterRainParams.x), 1.0) :
			(rainDebug == 2 ? vec3(clamp(rainField.g * 0.12 + 0.5, 0.0, 1.0), 0.15, 0.85) :
			(rainDebug == 3 ? vec3(0.5 + rainField.r * 0.12, 0.5 - abs(rainField.r) * 0.12, 0.5 - rainField.r * 0.12) :
			vec3(clamp(rainField.b, 0.0, 1.0), 0.15, 1.0 - clamp(rainField.b, 0.0, 1.0))));
		out_Color = vec4(LinearToScene(clamp(debugColor, 0.0, 1.0)), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}
	int shoreDebug = int(u_Water[WATER_U_SHORELINE].w + 0.5);
	if (shoreDebug > 0 && u_WaterPass.x < 0.5)
	{
		float contact = 1.0 - smoothstep(0.0, max(u_Water[WATER_U_SHORELINE].z, 0.25), var_ShoreData.x);
		vec3 debugColor = vec3(0.0);
		if (shoreDebug == 1) debugColor = vec3(clamp(u_Water[WATER_U_BODY_FLOW_DEPTH].z / 128.0, 0.0, 1.0));
		else if (shoreDebug == 2) debugColor = vec3(clamp(var_ShoreData.x / max(u_Water[WATER_U_SHORELINE].y, 1.0), 0.0, 1.0));
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
	float baseRoughness = u_Water[WATER_U_SURFACE].y;
	float alpha = sqrt(min(baseRoughness * baseRoughness * baseRoughness * baseRoughness +
		(variance.x + variance.y), 1.0));
	alpha = max(alpha, PBR_MIN_GGX_ALPHA);
	float roughness = sqrt(alpha);
	roughness = mix(roughness, 0.88, persistentFoam);
	roughness = mix(roughness, 0.78, whitewater);
	alpha = roughness * roughness;
	int windDebug = int(u_Water[WATER_U_WIND + 2].y + 0.5);
	if (windDebug > 0)
	{
		vec3 d = vec3(0.0);
		if (windDebug == 1)
			d = vec3(u_Water[WATER_U_WIND].xy * 0.5 + 0.5, clamp(u_Water[WATER_U_WIND].z, 0.0, 1.0));
		else if (windDebug == 2)
			d = vec3(u_Water[WATER_U_WIND + 1].xy * 0.5 + 0.5, 0.25);
		else if (windDebug == 3)
			d = vec3(u_Water[WATER_U_WIND + 1].zw * 0.5 + 0.5, 0.75);
		else
		{
			int source = int(u_Water[WATER_U_WIND].w + 0.5);
			d = source == 1 ? vec3(1.0, 0.75, 0.08) :
				(source == 2 ? vec3(0.1, 0.85, 1.0) :
				(source == 3 ? vec3(0.2, 0.45, 1.0) :
				(source == 4 ? vec3(0.65, 0.3, 1.0) : vec3(0.06))));
		d *= 0.2 + 0.8 * u_Water[WATER_U_WIND + 2].x;
		if (u_Water[WATER_U_WIND + 2].z < 0.5) d = mix(d, vec3(0.08), 0.75);
		}
		out_Color = vec4(LinearToScene(clamp(d, 0.0, 1.0)), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}
	int flowDebug = int(u_Water[WATER_U_FLOW + 1].z + 0.5);
	if (flowDebug > 0)
	{
		vec3 d = vec3(0.0);
		if (flowDebug == 1)
		{
			vec3 direction = length(u_Water[WATER_U_FLOW].xyz) > 1e-5 ? normalize(u_Water[WATER_U_FLOW].xyz) : vec3(0.0);
			d = direction * 0.5 + 0.5;
		}
		else if (flowDebug == 2)
		{
			float magnitude = clamp(u_Water[WATER_U_FLOW + 1].x / 32.0, 0.0, 1.0);
			d = vec3(magnitude, magnitude * magnitude, 1.0 - magnitude);
		}
		else if (flowDebug == 3)
		{
			int flowCode = int(u_Water[WATER_U_FLOW + 1].w + 0.5);
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
	int snellDebug = int(u_Water[WATER_U_DETAIL].w + 0.5);
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
	float maxPath = u_Water[WATER_U_DEBUG].w;

	// liquid optics
	vec3 sigma = u_Water[WATER_U_EXTINCTION].rgb * u_Water[WATER_U_OPTICS].z;
	vec3 albedo = u_Water[WATER_U_SCATTER].rgb;
	float g = u_Water[WATER_U_SCATTER].a;
	vec3 sunDir = normalize(u_PrimaryLightOrigin.xyz);
	vec3 sunColor = SceneToLinear(u_PrimaryLightColor);
	float sunShadow = 1.0;
	bool sun = u_Water[WATER_U_ENVIRONMENT].w > 0.5;
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
		if (WaterFlag(WATER_FLAG_SSR) && u_Water[WATER_U_OPTICS].y > 0.0)
		{
			float hitDistance;
			vec4 ssr = WaterSSR(P, R, Ng, N, roughness, hitDistance);
			float c = ssr.a * u_Water[WATER_U_OPTICS].y;
			env = mix(env, ssr.rgb, c);
			debugSource = mix(debugSource, vec3(1.0, 0.0, 0.0), c);
			ssrDebug = vec4(ssr.a > 0.0 ? vec3(0.0, ssr.a, 0.0) : vec3(0.35, 0.0, 0.0), 1.0);
		}
#endif
		reflection = env * u_Water[WATER_U_OPTICS].x;

		// ---- refraction: the refracted ray down to the scene behind the water. The first guess of its
		// length is the depth below the surface of the scene straight behind the pixel (bounded: behind
		// a far shore or the sky it is unknown and would push the sample to the horizon); two fixed point
		// steps take the depth found at the sample.
		float pathStraight = skyHere ? maxPath : (zHere - zSurface) * cameraDistance / zSurface;
		vec3 Rt = refract(-V, N, 1.0 / ior);
		float cosT = max(-dot(Rt, Ng), 0.05);
		float depthGuess = waterfall ? waterfallThickness :
			(skyHere ? 256.0 : min(pathStraight * max(dot(V, Ng), 0.0), 2048.0));
		float waterfallRefractionLod = waterfall ? u_Water[WATER_U_WATERFALL + 5].z : 1.0;
		vec2 uvR = waterfall && waterfallRefractionLod < 0.25 ? uv :
			WaterRefractedUV(P, Rt, depthGuess / cosT, uv);
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
		if (WaterFlag(WATER_FLAG_REJECT) && (!waterfall || waterfallRefractionLod > 0.75))
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
			pathLength = waterfallThickness * u_Water[WATER_U_WATERFALL + 4].x / cosT;
		else if (WaterIsSky(depthR))
			pathLength = maxPath;
		else
			pathLength = clamp(max(WaterDepthBelow(P, Ng, uvR, depthR), 0.0) / cosT, 0.0, maxPath);
		pathLength *= u_Water[WATER_U_OPTICS].w;

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
		if (!waterfall && u_Water[WATER_U_EXTINCTION].w > 0.5)
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
			float attenuation;
#if defined(USE_PHYSICAL_PBR)
			attenuation = PBRPointAttenuation(r, d2, u_PBRParams.y,
				u_Lights[i].physical.x);
#else
			attenuation = WaterLightAttenuation(r * r / d2);
#endif
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
			float airDistance = heightGuess / cosT;
			vec2 uvR = WaterRefractedUV(P, Rt, heightGuess / cosT, uv);
			float depthR = WaterSampleDepth(uvR);
			for (int i = 0; i < 2; i++)
			{
				if (WaterIsSky(depthR))
					break;
				float h = -WaterDepthBelow(P, Ng, uvR, depthR);
				if (h <= 0.0)
					break;
				airDistance = h / cosT;
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
			#if defined(USE_WATER_ATMOSPHERE)
			if (u_WaterOptics2.y > 0.5)
			{
				if (WaterIsSky(depthR))
					rawRefracted = AtmoSkyView(Rt) * u_Atmosphere[3].rgb * M_PI;
				else
				{
					float len = airDistance * u_Atmosphere[1].x * 0.001 * u_Atmosphere[1].y;
					vec3 sR, sM, airT;
					AtmosphereAerial(max(AtmoAltitude(P.z), -0.5), Rt.z, len,
						dot(Rt, u_Atmosphere[2].xyz), 1.0, sR, sM, airT);
					vec3 airS = (sR + sM) * u_Atmosphere[3].rgb * M_PI;
					rawRefracted = rawRefracted * airT + airS;
				}
			}
			#endif
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
		float pathScale = u_Water[WATER_U_OPTICS].w;
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
		if (WaterFlag(WATER_FLAG_SSR) && u_Water[WATER_U_OPTICS].y > 0.0)
		{
			float hitDistance;
			vec4 ssr = WaterSSR(P, R, Nside, N, roughness, hitDistance);
			float c = ssr.a * u_Water[WATER_U_OPTICS].y;
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
			vec2 offset = (WaterProject(P + Rt * 256.0) - uv) * u_Water[WATER_U_SURFACE].w;
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

	if (inside && sun && u_WaterOptics2.x > 0.5 && W < 0.999)
	{
		float transmissionAlpha = max(alpha, 0.015);
		// The microfacet BTDF already contains its Fresnel transmission term;
		// multiplying by the macro Fresnel weight here would count it twice.
		glint += WaterSunTransmission(N, V, sunDir,
			sunColor * sunShadow, transmissionAlpha, ior);
	}

	// Foam is a lit, rough diffuse layer in scene light units, never emission:
	// Lambert response to the same environment and sun as the water (the sun
	// irradiance is pi * sunColor, as in the in-liquid scattering above).
	// Bubbles on a thin layer transmit some of the water below where coverage
	// is partial; the coverage itself comes from the thresholded structure.
	vec3 foamIrradiance = ambient;
	if (sun) foamIrradiance += sunColor * clamp(dot(Nwater, sunDir), 0.0, 1.0) * sunShadow;
	vec3 foamColor = vec3(0.80, 0.84, 0.86) * foamIrradiance;
	vec3 color = (1.0 - W) * transmitted + W * reflection + glint;
	// Bulk whitewater is lit scattering inside aerated water, not emissive
	// surface paint. It suppresses clear refraction while retaining some of
	// the underlying water and reflection; persistent foam is layered later.
	vec3 whitewaterColor = vec3(0.55, 0.66, 0.70) * foamIrradiance;
	color = mix(color, whitewaterColor, whitewater * 0.72);
	color = mix(color, foamColor, persistentFoam * 0.92);
	// Channel B of the body-local interaction field is short-lived foam laid
	// down by energetic physical splash events.  It remains surface-local,
	// follows resolved body flow and opens up with the same structure.
	float splashFoam = clamp(interactionField.b * u_Water[WATER_U_GEOMETRY_FOAM].w + intersectionFoam, 0.0, 1.0);
	if (!inside && splashFoam > 0.0)
	{
		float splashCoverage = mix(WaterFoamCoverage(foamStructure1, splashFoam, foamSharp1),
			WaterFoamCoverage(foamStructure0, splashFoam, foamSharp0), foamPhaseWeight);
		color = mix(color, foamColor, splashCoverage * 0.92);
		persistentFoam = max(persistentFoam, splashCoverage);
	}

	int waterfallDebug = int(u_Water[WATER_U_WATERFALL].z + 0.5);
	if (waterfall && waterfallDebug > 0 && u_WaterPass.x < 0.5)
	{
		vec3 d = vec3(0.0);
		if (waterfallDebug == 1) d = vec3(0.05, 0.85, 1.0);
		else if (waterfallDebug == 2) d = normalize(u_Water[WATER_U_WATERFALL + 1].xyz) * 0.5 + 0.5;
		else if (waterfallDebug == 3) d = vec3(waterfallAlong, 1.0 - waterfallAlong, 0.15);
		else if (waterfallDebug == 4) d = normalize(var_GeometryDisplacement + vec3(1e-5)) * 0.5 + 0.5;
		else if (waterfallDebug == 5) d = vec3(clamp(waterfallThickness / max(u_Water[WATER_U_WATERFALL].w * 1.25, 1e-3), 0.0, 1.0));
		else if (waterfallDebug == 6) d = rawRefracted;
		else if (waterfallDebug == 7) d = vec3(waterfallTurbulence, 0.2 * waterfallTurbulence, 1.0 - waterfallTurbulence);
		else if (waterfallDebug == 8) d = vec3(waterfallAeration);
		else if (waterfallDebug == 9) d = vec3(waterfallFoamSource, waterfallFoamSource * 0.7, 0.0);
		else if (waterfallDebug == 10) d = vec3(waterfallSpraySource, 0.25 * waterfallSpraySource, 0.0);
		else if (waterfallDebug == 11) d = vec3(waterfallImpact, 0.0, 0.8 * waterfallImpact);
		else
		{
			float spacing = u_Water[WATER_U_WATERFALL].y < 0.5 ? 128.0 : (u_Water[WATER_U_WATERFALL].y > 1.5 ? 32.0 : 64.0);
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
	int waveDebug = int(u_Water[WATER_U_WAVE_DEBUG].x + 0.5);
	int geometryDebug = int(u_Water[WATER_U_GEOMETRY_FOAM].z + 0.5);
	if (geometryDebug == 2)
	{
		out_Color = vec4(LinearToScene(vec3(min(length(var_GeometryDisplacement) * 0.25, 1.0))), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}
	if (geometryDebug == 4)
	{
		vec3 bodyColor = vec3(fract(u_Water[WATER_U_WAVE_DEBUG].y * 0.37),
			fract(u_Water[WATER_U_WAVE_DEBUG].y * 0.61), fract(u_Water[WATER_U_WAVE_DEBUG].y * 0.83));
		out_Color = vec4(LinearToScene(mix(bodyColor, vec3(1.0, 0.0, 0.0),
			step(0.02, var_GeometrySkirt))), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}
	if (u_Water[WATER_U_WAVE_CONTROL].x > 0.5 && waveDebug > 0 && waveDebug != 8)
	{
		vec3 d = vec3(0.0);
		if (waveDebug == 1) d = vec3(0.5 + waves.height * 0.1);
		else if (waveDebug == 2) d = normalize(Ng - macroSlope.x * Tw - macroSlope.y * Bw) * 0.5 + 0.5;
		else if (waveDebug == 3) d = microNormal * 0.5 + 0.5;
		else if (waveDebug == 4) d = Nwater * 0.5 + 0.5;
		else if (waveDebug == 5) d = vec3(fract(u_Water[WATER_U_BODY_FLOW_DEPTH].w * 0.17), fract(u_Water[WATER_U_WAVE_DEBUG].y * 0.37), 0.5);
		else if (waveDebug == 6) d = vec3(waves.attenuation);
		else if (waveDebug == 7) d = vec3(min(length(waves.displacement) * 2.0, 1.0));
		else if (waveDebug == 9) d = vec3(clamp(u_Water[WATER_U_WAVE_DEBUG].w * 0.5, 0.0, 1.0));
		else if (waveDebug == 10) d = vec3(clamp(u_Water[WATER_U_GEOMETRY_FOAM].x * 0.1, 0.0, 1.0));
		out_Color = vec4(LinearToScene(d), sceneHere.a);
		out_Glow = vec4(0.0);
		return;
	}

	out_Color = vec4(LinearToScene(color), sceneHere.a);
	out_Glow = vec4(LinearToScene(((1.0 - W) * transmittedGlow + glint) *
		(1.0 - persistentFoam) * (1.0 - 0.8 * whitewater)), 0.0);
}
