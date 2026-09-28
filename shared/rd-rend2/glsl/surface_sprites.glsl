/*[Vertex]*/
in vec4 attr_Position; // x, y, z, random value [0.0, 1.0]
in vec3 attr_Normal;
in vec3 attr_Color;
in vec4 attr_Position2; // width, height, skew.x, skew.y

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

layout(std140) uniform SurfaceSprite
{
	vec2  u_FxGrow;
	float u_FxDuration;
	float u_FadeStartDistance;
	float u_FadeEndDistance;
	float u_FadeScale;
	float u_Wind;
	float u_WindIdle;
	float u_FxAlphaStart;
	float u_FxAlphaEnd;
};

// These match the visible camera even while the sprite is submitted to a
// sun-cascade depth view.  The projection itself still comes from Camera.
uniform vec3 u_SpriteViewOrigin;
uniform vec3 u_SpriteViewLeft;
uniform vec3 u_SpriteViewUp;

#if defined(AUTO_GRASS)
// r_autoGrass: x = angular spacing (180 / x degrees per instance),
// y = lod distance of the third card (0 = no lod), z = debug mode,
// w = card width scale
uniform vec4 u_AutoGrass;
#endif

// r_foliageWind: x, y = unit wind direction, z = strength, w = speed
uniform vec4 u_FoliageWind;
// x = mode (0 legacy sway, 1 coherent breeze), y = debug color mode,
// z = frozen time (seconds), w = 1 when the time is frozen
uniform vec4 u_FoliageWindParams;

// r_foliageInteraction (foliage_interact.glsl): x = 1 when the character
// colliders bend these sprites, y = 1 for the contact heat debug color
uniform vec4 u_FoliageInteract;

#if defined(VELOCITY_PASS)
layout(std140) uniform TemporalInfo
{
	mat4 u_previousViewProjectionMatrix;
	vec2 u_currentJitter;
	vec2 u_previousJitter;
	float u_previousFrameTime;
};
out vec4 var_Position;
out vec4 var_prevPosition;
#endif

out vec2 var_TexCoords;
out float var_Alpha;
out vec3 var_Color;

#if defined(FX_SPRITE)
out float var_Effectpos;
#endif

#if defined(USE_FOG)
out vec3 var_WSPosition;
#endif

#if defined(AUTO_GRASS)
// World stable card direction: the random horizontal direction stored at map
// load, rotated by 180/n degrees per instance.  Independent of camera and time,
// so current and previous frame poses match.
vec2 AutoGrassCardDir()
{
	vec2 base = normalize(attr_Normal.xy);
	float angle = float(gl_InstanceID) * (3.14159265 / max(u_AutoGrass.x, 1.0));
	float c = cos(angle);
	float s = sin(angle);
	return vec2(base.x * c - base.y * s, base.x * s + base.y * c);
}
#endif

#if !defined(FACE_UP) && !defined(FX_SPRITE)
// Lattice hash, wrapped to 1024 cells so large world coordinates stay exact
float WindHash(in ivec2 cell)
{
	uint h = uint(cell.x & 1023) | (uint(cell.y & 1023) << 10u);
	h *= 2654435761u;
	h ^= h >> 15u;
	h *= 2246822519u;
	h ^= h >> 13u;
	return float(h) * (1.0 / 4294967295.0);
}

// Smooth value noise in [0, 1]
float WindNoise(in vec2 x)
{
	vec2 i = floor(x);
	vec2 f = x - i;
	f = f * f * (3.0 - 2.0 * f);
	ivec2 c = ivec2(i);
	float a = WindHash(c);
	float b = WindHash(c + ivec2(1, 0));
	float d = WindHash(c + ivec2(0, 1));
	float e = WindHash(c + ivec2(1, 1));
	return mix(mix(a, b, f.x), mix(d, e, f.x), f.y);
}

// debug output of the last FoliageWind evaluation
float g_WindGust = 0.0;
vec2 g_WindBend = vec2(0.0);

// r_foliageInteraction: which collider set CalculateVertexOffset uses (the
// velocity pass evaluates the previous frame too) and the contact it found
bool g_FoliagePrevious = false;
float g_FoliageHeat = 0.0;

// r_foliageWind 1: coherent breeze.  A pure function of the sprite anchor, its
// stable random seed and time; no camera or instance input, so all cards of a
// tuft, the shadow views and both velocity frames get the same displacement.
// Returns the offset of the upper vertices.
vec3 FoliageWind(in vec2 anchor, in float seed, in float t, in float height)
{
	vec2 dir = u_FoliageWind.xy;
	vec2 side = vec2(-dir.y, dir.x);
	float tw = t * u_FoliageWind.w;

	// gust field scrolling downwind: patchy ~512u swells (value noise) moving
	// ~60u/s, plus ~180u gust fronts moving ~70u/s whose crests are bent
	// sideways so they don't form straight rows
	float along = dot(anchor, dir);
	float across = dot(anchor, side);
	float gustL = WindNoise(anchor * (1.0 / 512.0) - dir * (tw * 0.12));
	float gustS = sin(along * 0.035 - tw * 2.45 + 1.3 * sin(across * 0.013 + tw * 0.21));
	float gust = max(0.25 + 0.75 * gustL + 0.18 * gustS, 0.0);

	// the blade leans downwind and bobs with a ripple travelling along the wind
	// (~125u, ~0.5Hz, small per blade phase offset); a faint cross wind flutter
	// at a per blade rate breaks up the rows
	float ripple = sin(tw * 3.0 - along * 0.05 + seed * 1.5);
	float lean = gust * (0.6 + 0.3 * ripple);
	float flutter = 0.2 * gust * sin(tw * (4.5 + 2.0 * seed) + seed * 6.2832);
	g_WindGust = gustL;
	g_WindBend = vec2(lean, flutter);

	float h = abs(height);
	vec2 disp = (dir * lean + side * flutter) * (h * 0.12 * u_FoliageWind.z * u_WindIdle);

	// keep extreme ssWind values sane, then bend instead of stretching: pull
	// the tip back towards the anchor height
	float len2 = dot(disp, disp);
	float maxLen = 0.5 * h;
	if (len2 > maxLen * maxLen)
	{
		disp *= maxLen * inversesqrt(len2);
		len2 = maxLen * maxLen;
	}
	float drop = len2 / (2.0 * max(h, 1.0));
	return vec3(disp, -drop * sign(height));
}
#endif

vec3 CalculateVertexOffset( in int vertex_id, in float sprite_time, in float wind_time, in float fadeScale)
{
	float width = attr_Position2.x;
	float height = attr_Position2.y;
	vec2 skew = attr_Position2.zw;
	
	width += u_FadeScale * fadeScale * width;

#if defined(FX_SPRITE)
	var_Effectpos = fract((sprite_time+10000.0*attr_Position.w) / u_FxDuration);
	width += var_Effectpos * width * u_FxGrow.x;
	height += var_Effectpos * height * u_FxGrow.y;
#endif

#if !defined(FACE_FLATTENED)
	float halfWidth = width * 0.5;
	vec3 offsets[] = vec3[](
#if defined(FACE_UP)
		vec3( halfWidth, -halfWidth, 0.0),
		vec3( halfWidth,  halfWidth, 0.0),
		vec3(-halfWidth,  halfWidth, 0.0),
		vec3(-halfWidth, -halfWidth, 0.0)
#else
		vec3( halfWidth, 0.0, 0.0),
		vec3( halfWidth, 0.0, height),
		vec3(-halfWidth, 0.2, height), // Offset this upper vertex to make sprite visable from above
		vec3(-halfWidth, 0.0, 0.0)
#endif
	);
#else
	float offsetValue = mix(width, height, attr_Position.w);
	vec3 offsets[] = vec3[](
		vec3( offsetValue, 0.0, 0.0),
		vec3( offsetValue, 0.0, height),
		vec3(-offsetValue, 0.2, height), // Offset this upper vertex to make sprite visable from above
		vec3(-offsetValue, 0.0, 0.0)
	);
#endif

	vec3 offset = offsets[vertex_id];

#if defined(FACE_CAMERA)
	offset = (offset.x * normalize(u_SpriteViewLeft)) + (offset.z * normalize(u_SpriteViewUp));
#elif defined(FACE_FLATTENED)
	// Make this sprite face in some direction
	vec3 fwdVec = cross(attr_Normal, vec3(0.0, 0.0, 1.0));
	offset.xy = (offset.x * attr_Normal.xy) + (offset.y * width * fwdVec.xy);
#elif defined(AUTO_GRASS)
	// Cards share the anchor, so the instances form a cross / tri-card tuft
	vec2 cardDir = AutoGrassCardDir();
	vec2 cardFwd = vec2(-cardDir.y, cardDir.x);
	offset.xy = (offset.x * u_AutoGrass.w * cardDir) + (offset.y * width * cardFwd);
#elif !defined(FACE_UP)
	// Make this sprite face in some direction in direction of the camera
	vec3 lftVec = normalize(u_SpriteViewLeft);
	vec3 fwdVec = cross(lftVec, vec3(0.0, 0.0, 1.0));
	offset.xy = (offset.x * normalize(attr_Normal.xy + 2.0 * lftVec.xy)) + (offset.y * width * fwdVec.xy);
#endif

#if !defined(FACE_UP) && !defined(FX_SPRITE)
	float isLowerVertex = float(offset.z == 0.0);
	offset.xy += mix(skew, vec2(0.0), isLowerVertex);
	bool interaction = u_FoliageInteract.x > 0.5;
	// the tip offset of the wind, the stem the colliders bend further
	vec3 windTip = vec3(0.0);
	if (interaction && FoliageInteractionNoWind())
	{
		// r_foliageInteractionDebug 4: interaction only
	}
	else if (u_FoliageWindParams.x < 0.5)
	{
		float angle = (attr_Position.x + attr_Position.y) * 0.02 + (sprite_time * 0.0015);
		float windsway = mix(height* u_WindIdle * 0.075, 0.0, isLowerVertex);
		offset.xy += vec2(cos(angle), sin(angle)) * windsway;
		windTip.xy = vec2(cos(angle), sin(angle)) * (height * u_WindIdle * 0.075);
	}
	else if (u_WindIdle > 0.0)
	{
		vec3 wind = FoliageWind(attr_Position.xy, attr_Position.w, wind_time, height);
		offset += wind * (1.0 - isLowerVertex);
		windTip = wind;
	}

	// r_foliageInteraction: the character colliders bend the stem (anchor to
	// tip, skew and wind included) further. Everything here depends on the
	// anchor only, so all cards of a tuft move the top edge by the same
	// vector; the bottom vertices stay on the ground. Near the ground (40 % of
	// the height) is what the colliders test, so the feet part the grass.
	if (interaction)
	{
		vec3 stem = vec3(skew, height) + windTip;
		vec3 q = attr_Position.xyz + vec3(0.0, 0.0, 0.4 * height);
		vec2 bend = FoliageCharacterBend(q, attr_Position.xy, g_FoliagePrevious, g_FoliageHeat);
		offset += (FoliageApplyBend(stem, bend, 1.0) - stem) * (1.0 - isLowerVertex);
	}
#endif
	return offset;
}

void main()
{
	vec3 V = u_SpriteViewOrigin - attr_Position.xyz;
	float distanceToCamera = length(V);
	float fadeScale = smoothstep(u_FadeStartDistance, u_FadeEndDistance,
						distanceToCamera);
	
	if (fadeScale >= 1.0)
	{
		gl_Position = vec4(0.0);
		return;
	}

#if defined(AUTO_GRASS)
	// adaptive lod: the third card erodes like the distance fade, then
	// collapses to a zero area primitive
	float cardFade = 0.0;
	if (gl_InstanceID >= 2 && u_AutoGrass.y > 0.0)
	{
		cardFade = smoothstep(u_AutoGrass.y, u_AutoGrass.y * 1.33, distanceToCamera);
		if (cardFade >= 1.0)
		{
			gl_Position = vec4(0.0);
			return;
		}
	}
#endif

	float sprite_time = u_frameTime * 1000.0;
	// r_foliageWindDebug 3 freezes the breeze in both frames
	bool windFrozen = u_FoliageWindParams.w > 0.5;
	float wind_time = windFrozen ? u_FoliageWindParams.z : u_frameTime;
	int vertex_id = gl_VertexID % 4;
	vec3 offset = CalculateVertexOffset(vertex_id, sprite_time, wind_time, fadeScale);
#if !defined(FACE_UP) && !defined(FX_SPRITE)
	float windGust = g_WindGust;
	vec2 windBend = g_WindBend;
#endif

	vec4 worldPos = vec4(attr_Position.xyz + offset, 1.0);
	gl_Position = u_viewProjectionMatrix * worldPos;
#if defined(USE_FOG)
	var_WSPosition = worldPos.xyz;
#endif

#if !defined(FACE_UP) && !defined(FX_SPRITE)
	float foliageHeat = g_FoliageHeat;
#endif

#if defined(VELOCITY_PASS)
	var_Position = gl_Position;
	sprite_time = u_previousFrameTime * 1000.0;
	wind_time = windFrozen ? u_FoliageWindParams.z : u_previousFrameTime;
#if !defined(FACE_UP) && !defined(FX_SPRITE)
	g_FoliagePrevious = true;
#endif
	offset = CalculateVertexOffset(vertex_id, sprite_time, wind_time, fadeScale);
	worldPos = vec4(attr_Position.xyz + offset, 1.0);
	var_prevPosition = u_previousViewProjectionMatrix * worldPos;
#endif

	const vec2 texcoords[] = vec2[](
		vec2(1.0, 1.0),
		vec2(1.0, 0.0),
		vec2(0.0, 0.0),
		vec2(0.0, 1.0)
	);
	var_TexCoords = texcoords[vertex_id];
	var_Color = attr_Color;
	var_Alpha = 1.0 - fadeScale;

#if !defined(FACE_UP) && !defined(FX_SPRITE)
	if (u_FoliageWindParams.x > 0.5 && u_FoliageWindParams.y > 0.5)
	{
		if (u_FoliageWindParams.y < 3.0)
		{
			// red = downwind lean, green = cross wind flutter, blue = calm;
			// sprites without ssWind stay dark blue
			float lean = clamp(windBend.x * u_WindIdle * u_FoliageWind.z, 0.0, 1.0);
			var_Color = vec3(lean, 0.5 + 2.0 * windBend.y * u_WindIdle * u_FoliageWind.z, 1.0 - lean);
		}
		else
		{
			// large gust wave
			var_Color = vec3(windGust);
		}
	}
	// r_foliageInteractionDebug 8: collider contact, blue 0 -> yellow 1
	if (u_FoliageInteract.y > 0.5)
		var_Color = mix(vec3(0.05, 0.1, 0.8), vec3(1.0, 0.85, 0.1), foliageHeat);
#endif

#if defined(AUTO_GRASS)
	var_Alpha *= 1.0 - cardFade;

	int debugMode = int(u_AutoGrass.z + 0.5);
	if (debugMode == 1)
	{
		// card index
		const vec3 cardColors[] = vec3[](
			vec3(1.0, 0.15, 0.1), vec3(0.1, 1.0, 0.15), vec3(0.15, 0.3, 1.0));
		var_Color = cardColors[min(gl_InstanceID, 2)];
	}
	else if (debugMode == 2)
	{
		// card orientation, 180 degree symmetric
		vec2 dir = AutoGrassCardDir();
		dir *= sign(dir.x + 1e-4);
		var_Color = vec3(0.5 + 0.5 * dir.x, 0.5 + 0.5 * dir.y, 0.5 - 0.5 * dir.y);
	}
	else if (debugMode == 6)
		var_Color = vec3(0.1, 1.0, 0.2);	// lod: three cards
	else if (debugMode == 7)
		var_Color = vec3(1.0, 0.55, 0.05);	// lod: two cards
#endif

}

/*[Fragment]*/
uniform sampler2D u_DiffuseMap;

#if defined(VELOCITY_PASS)
layout(std140) uniform TemporalInfo
{
	mat4 u_previousViewProjectionMatrix;
	vec2 u_currentJitter;
	vec2 u_previousJitter;
	float u_previousFrameTime;
};
in vec4 var_Position;
in vec4 var_prevPosition;
#endif

in vec2 var_TexCoords;
in vec3 var_Color;
in float var_Alpha;

#if defined(FX_SPRITE)
in float var_Effectpos;
#endif

#if defined(USE_FOG)
in vec3 var_WSPosition;
#endif

layout(std140) uniform SurfaceSprite
{
	vec2  u_FxGrow;
	float u_FxDuration;
	float u_FadeStartDistance;
	float u_FadeEndDistance;
	float u_FadeScale;
	float u_Wind;
	float u_WindIdle;
	float u_FxAlphaStart;
	float u_FxAlphaEnd;
};

#if defined(USE_FOG)
layout(std140) uniform Camera
{
	mat4 u_viewProjectionMatrix;
	vec4 u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
};

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
#endif

#if defined(USE_ALPHA_TEST)
uniform int u_AlphaTestType;
#endif
uniform vec4 u_FoliageDebug;

out vec4 out_Color;
#if !defined(VELOCITY_PASS)
out vec4 out_Glow;
#endif

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

	bool intersects = (t > 0.0 && t <= 1.0);
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
#if defined(USE_ALPHA_TEST)
	float alphaTestValue = 0.5;
	if (u_AlphaTestType == ALPHA_TEST_GT0)
	{
		alphaTestValue = 0.0;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE192)
	{
		alphaTestValue = 0.75;
	}
#else
	const float alphaTestValue = 0.5;
#endif

	out_Color = texture(u_DiffuseMap, var_TexCoords);
	out_Color.rgb *= var_Color;
	out_Color.a *= var_Alpha*(1.0 - alphaTestValue) + alphaTestValue;

#if defined(FX_SPRITE)
	float fxalpha = u_FxAlphaEnd - u_FxAlphaStart;
	if (u_FxAlphaEnd < 0.05)
	{
	if (var_Effectpos > 0.5)
		out_Color.a *= u_FxAlphaStart + (fxalpha * (var_Effectpos - 0.5) * 2.0);
	else
		out_Color.a *= u_FxAlphaStart + (fxalpha * (0.5 - var_Effectpos) * 2.0);
	}
	else
	{
		out_Color.a *= u_FxAlphaStart + (fxalpha * var_Effectpos);
	}
#endif

#if defined(USE_ALPHA_TEST)
	if (u_AlphaTestType == ALPHA_TEST_GT0)
	{
		if (out_Color.a == 0.0)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_LT128)
	{
		if (out_Color.a >= 0.5)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE128)
	{
		if (out_Color.a < 0.5)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE192)
	{
		if (out_Color.a < 0.75)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_E255)
	{
		if (out_Color.a < 1.00)
			discard;
	}
#endif

#if !defined(VELOCITY_PASS)
	if (u_FoliageDebug.a > 0.0)
		out_Color.rgb = u_FoliageDebug.rgb;
#endif

#if defined(USE_FOG)
#if defined(USE_FROXEL_FOG)
	// froxel volume of the main view (r_volumetricFog 2): transmittance and
	// in-scattering up to the sprite, or none when the composite applies it
	if (u_FroxelFogMode != 0)
	{
		if (u_FroxelFogMode == 1)
		{
			vec4 froxelFog = FroxelFog(var_WSPosition);
	#if defined(ADDITIVE_BLEND)
			out_Color.rgb *= froxelFog.a;
	#else
			out_Color.rgb = out_Color.rgb * froxelFog.a + froxelFog.rgb;
	#endif
		}
	#if defined(USE_FROXEL_RGB)
		else if (u_FroxelFogMode == 3)
		{
			// RGB extinction (r_volumetricFogRGB)
			vec3 froxelT;
			vec3 froxelS = FroxelFogRGB(var_WSPosition, froxelT);
		#if defined(ADDITIVE_BLEND)
			out_Color.rgb *= froxelT;
		#else
			out_Color.rgb = out_Color.rgb * froxelT + froxelS;
		#endif
		}
	#endif
	}
	else
	{
#endif
	Fog fog = u_Fogs[u_FogIndex];
	vec4 fogColorOpacity = CalcFog(u_ViewOrigin, var_WSPosition, fog);
#if defined(ADDITIVE_BLEND)
	out_Color.rgb *= fogColorOpacity.rgb * (1.0 - fogColorOpacity.a);
#else
#if defined(USE_VOLUMETRIC_FOG)
	out_Color.rgb = out_Color.rgb * (1.0-fogColorOpacity.a) + fogColorOpacity.rgb;
#else
	out_Color.rgb = mix(out_Color.rgb, fogColorOpacity.rgb, fogColorOpacity.a);
#endif
#endif
#if defined(USE_FROXEL_FOG)
	}
#endif
#endif

#if defined(ADDITIVE_BLEND)
	out_Color.rgb *= out_Color.a;
#endif

#if defined(VELOCITY_PASS)
	vec2 currentPos = (var_Position.xy / var_Position.w) * 0.5 + 0.5;
	vec2 prevPos = (var_prevPosition.xy / var_prevPosition.w) * 0.5 + 0.5;
	vec2 motionVector = currentPos - prevPos;

	motionVector -= u_currentJitter / r_FBufScale.xy;
	motionVector -= u_previousJitter / r_FBufScale.xy;

	out_Color = vec4(motionVector, 0.0, 1.0);
#else
	out_Glow = vec4(0.0);
#endif
	
}
