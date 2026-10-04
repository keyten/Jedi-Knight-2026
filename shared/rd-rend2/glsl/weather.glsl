/*[Vertex]*/
uniform vec2 u_ZoneOffset[9];

uniform sampler2D u_ShadowMap;
uniform mat4 u_ShadowMvp;
uniform vec4 u_ViewInfo;

// 0 legacy particle (snow, spacedust, sand, fog), 1 rain streak (r_rainStreaks)
uniform int u_WeatherType;
uniform vec4 u_RainStreak;	// width scale, length scale, weather depth range, sRGB coverage
uniform vec3 u_CameraVelocity;

in vec3 attr_Position;
in vec3 attr_Color;  // velocity

out vec3 var_Velocity;
out int var_Culled;
out float var_DepthSample;
out vec4 var_Rand;

#if defined(USE_LIQUIDS)
// r_volumetricWater (tr_liquid.cpp RB_LiquidWeatherSetupDraw): no weather inside a liquid brush.
// The Liquids block of liquid_common.glsl (the same layout, a fragment library there); only the
// brush count, bounds and planes are read here.
layout(std140) uniform Liquids
{
	vec4 u_LiquidParams;		// x: brushes in the block
	vec4 u_LiquidCaustics;
	vec4 u_LiquidView;
	vec4 u_LiquidMaterial[6];
	vec4 u_LiquidMins[MAX_GPU_LIQUIDS];	// bounds, w: first plane
	vec4 u_LiquidMaxs[MAX_GPU_LIQUIDS];	// w: planes + 64 * medium + 256 * class
	ivec4 u_LiquidSlices[FROXEL_MAX_SLICES / 4];
};
uniform samplerBuffer u_LiquidPlanes;
uniform vec4 u_LiquidSurface;	// x 1 = drop weather inside the liquids of the block

bool WeatherInLiquid(in vec3 p)
{
	if (u_LiquidSurface.x < 0.5)
		return false;
	int n = int(u_LiquidParams.x);
	for (int i = 0; i < n; i++)
	{
		if (any(lessThan(p, u_LiquidMins[i].xyz)) || any(greaterThan(p, u_LiquidMaxs[i].xyz)))
			continue;
		int first = int(u_LiquidMins[i].w);
		int count = int(u_LiquidMaxs[i].w) & 63;
		bool inside = true;
		for (int k = 6; k < count && inside; k++)
		{
			vec4 plane = texelFetch(u_LiquidPlanes, first + k);
			inside = dot(plane.xyz, p) - plane.w <= 0.0;
		}
		if (inside)
			return true;
	}
	return false;
}
#endif

// Per particle variation. The seed is the VBO slot (gl_VertexID includes the
// chunk's first vertex): transform feedback keeps the slot order, so a
// particle keeps its look while it falls and moves.
uint HashUint(uint x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

void main()
{
	gl_Position = vec4(
		attr_Position.xy + u_ZoneOffset[0],
		attr_Position.z,
		1.0);

	if (u_WeatherType == 1)
	{
		uint h = HashUint(uint(gl_VertexID) * 747796405u + 2891336453u);
		var_Rand = vec4(uvec4(h, h >> 8, h >> 16, h >> 24) & uvec4(255u)) / 255.0;

		// velocity relative to the moving camera, never degenerating to a
		// horizontal velocity (spawned particles start at rest)
		vec3 velocity = attr_Color - u_CameraVelocity;
		float fallSpeed = max(-attr_Color.z, 0.0);
		velocity.z = min(velocity.z, -max(0.3 * fallSpeed, 0.05));

		// tilt: the legacy offset.xy += offset.z * v.xy / v.z, bounded at 3:1
		vec2 tilt = velocity.xy / velocity.z;
		float tiltLength = length(tilt);
		if (tiltLength > 3.0)
			tilt *= 3.0 / tiltLength;

		// exposure time model: the vertical extent follows the fall speed,
		// the tilt then makes the whole streak follow |v|
		float halfLength = u_ViewInfo.y * u_RainStreak.y * mix(0.85, 1.15, var_Rand.y) *
			clamp(-velocity.z / 1.4, 0.6, 2.0);
		var_Velocity = vec3(tilt, halfLength);

		// one weather depth sample under the streak's lower end
		vec3 bottom = gl_Position.xyz + vec3(-halfLength * tilt, -halfLength);
		vec3 top = gl_Position.xyz + vec3(halfLength * tilt, halfLength);
		vec4 depthPosition = u_ShadowMvp * vec4(bottom, 1.0);
		var_DepthSample = texture(u_ShadowMap, depthPosition.xy / depthPosition.w * 0.5 + 0.5).r;
		depthPosition = u_ShadowMvp * vec4(top, 1.0);
		var_Culled = int((depthPosition.z / depthPosition.w * 0.5 + 0.5) > var_DepthSample);
#if defined(USE_LIQUIDS)
		// the streak's center under a liquid surface: under water, not drawn
		if (WeatherInLiquid(gl_Position.xyz))
			var_Culled = 1;
#endif
		return;
	}

	var_Rand = vec4(0.0);
	var_DepthSample = 0.0;

	var_Velocity = attr_Color;
	var_Velocity.z = min(-0.00001, var_Velocity.z);

	vec4 velocitiyOffset = u_ViewInfo.y * vec4(-var_Velocity.xy/var_Velocity.z, var_Velocity.z, 0.0);
	velocitiyOffset.xyz = mix(vec3(0.0), velocitiyOffset.xyz, float(attr_Color.z != 0.0));
	var_Velocity.z *= u_ViewInfo.z;

	vec4 depthPosition = u_ShadowMvp * (gl_Position + velocitiyOffset);
	depthPosition.xyz = depthPosition.xyz / depthPosition.w * 0.5 + 0.5;
	float depthSample = texture(u_ShadowMap, depthPosition.xy).r;

	var_Culled = int(depthPosition.z > depthSample);
	if (var_Culled == 0)
	{
		depthPosition = u_ShadowMvp * (gl_Position + velocitiyOffset -(vec4(0.0, 0.0, u_ViewInfo.y, 0.0)));
		var_Culled -= int((depthPosition.z / depthPosition.w * 0.5 + 0.5) > depthSample);
	}
#if defined(USE_LIQUIDS)
	if (WeatherInLiquid(gl_Position.xyz))
		var_Culled = 1;
#endif
}

/*[Geometry]*/
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

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
	vec4 _u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
};

uniform vec4 u_ViewInfo;
uniform vec4 u_Color;
uniform mat4 u_ShadowMvp;

uniform int u_WeatherType;
uniform vec4 u_RainStreak;	// width scale, length scale, weather depth range, sRGB coverage
uniform vec4 u_RainShade;	// opacity, lighting mix, world size of a pixel at distance 1, debug
uniform vec4 u_RainLight;	// light without a grid, light grid valid
uniform sampler3D u_VolumetricLightMap;
uniform vec3 u_LightGridOrigin;
uniform vec3 u_LightGridCellInverseSize;

in vec3 var_Velocity[];
in int  var_Culled[];
in float var_DepthSample[];
in vec4 var_Rand[];

out vec3 var_TexCoordAlpha;
out vec3 var_RainContact;		// clearance above the rain occluder (world units), near fade, far fade
flat out vec4 var_RainColor;	// premultiplied tint * light, contact fade length
flat out vec4 var_RainDebug;

float Luminance(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// Lightmap equivalent light at the particle: 1 = a surface at full texture
// brightness, which is the brightness the legacy tint assumed.
vec3 RainLight(vec3 position)
{
	vec3 light = u_RainLight.rgb;
	if (u_RainLight.w > 0.5)
	{
		vec3 gridSize = vec3(textureSize(u_VolumetricLightMap, 0));
		vec3 cell = (position - u_LightGridOrigin) * u_LightGridCellInverseSize;
		light = texture(u_VolumetricLightMap, (cell + 0.5) / gridSize).rgb;
	}

	// forward scattering toward the primary light. The light grid says how
	// lit the air around the drop is; use it as the sun visibility.
	float sunLuminance = Luminance(u_PrimaryLightAmbient + u_PrimaryLightColor);
	if (sunLuminance > 1e-4)
	{
		float sunVisibility = clamp(Luminance(light) / sunLuminance, 0.0, 1.0);
		vec3 viewDir = normalize(position - u_ViewOrigin);
		float forward = pow(max(dot(viewDir, u_PrimaryLightOrigin.xyz), 0.0), 8.0);
		light += u_PrimaryLightColor * (0.6 * sunVisibility * forward);
	}
	return mix(vec3(1.0), light, u_RainShade.y);
}

float WeatherDepth(vec3 position)
{
	vec4 depthPosition = u_ShadowMvp * vec4(position, 1.0);
	return depthPosition.z / depthPosition.w * 0.5 + 0.5;
}

void EmitRain()
{
	const vec2 corners[] = vec2[](
		vec2(-1.0, -1.0),
		vec2( 1.0, -1.0),
		vec2(-1.0,  1.0),
		vec2( 1.0,  1.0)
	);
	const vec2 texcoords[] = vec2[](
		vec2(1.0, 1.0),
		vec2(0.0, 1.0),
		vec2(1.0, 0.0),
		vec2(0.0, 0.0)
	);

	vec3 P = gl_in[0].gl_Position.xyz;
	vec3 V = u_ViewOrigin - P;
	vec2 toCamera = normalize(vec2(V.y, -V.x));
	float viewDistance = length(V);
	vec4 rand = var_Rand[0];

	// drops next to the camera must not become screen tall bars
	vec2 tilt = var_Velocity[0].xy;
	float halfLength = min(var_Velocity[0].z, 2.0 + 0.35 * viewDistance);

	// at least a pixel wide: thinner far streaks trade width for opacity
	// instead of sparkling
	float halfWidth = u_ViewInfo.x * u_RainStreak.x * mix(0.85, 1.15, rand.x);
	float minHalfWidth = 0.5 * viewDistance * u_RainShade.z;
	float subPixel = 1.0;
	if (halfWidth < minHalfWidth)
	{
		subPixel = halfWidth / minHalfWidth;
		halfWidth = minHalfWidth;
	}

	float brightness = mix(0.85, 1.05, rand.z);
	float opacity = u_Color.a * u_RainShade.x * mix(0.8, 1.0, rand.w) * subPixel;
	vec3 light = RainLight(P);
	vec3 tint = u_Color.rgb / max(u_Color.a, 1e-4);
	var_RainColor = vec4(tint * light * brightness, min(8.0, halfLength));
	var_RainDebug = vec4(light, subPixel);
	if (u_RainShade.w == 5.0)
		var_RainDebug = vec4(rand.xyz, 1.0);

	float fadeDistance = u_ViewInfo.w;
	for (int i = 0; i < 4; ++i)
	{
		vec3 offset = vec3(corners[i].x * halfWidth * toCamera, corners[i].y * halfLength);
		offset.xy += offset.z * tilt;
		vec3 worldPos = P + offset;
		gl_Position = u_viewProjectionMatrix * vec4(worldPos, 1.0);

		float vertexDistance = distance(u_ViewOrigin, worldPos);
		float nearFade = smoothstep(12.0, 48.0, vertexDistance);
		// the legacy linear fade reshaped: full near, about as dim as before
		// in the far half (0.61 vs 0.5 at half the fade distance)
		float farFade = 1.0 - smoothstep(0.15 * fadeDistance, fadeDistance, vertexDistance);
		float clearance = (var_DepthSample[0] - WeatherDepth(worldPos)) * u_RainStreak.z;

		var_TexCoordAlpha = vec3(texcoords[i], opacity * nearFade * farFade);
		var_RainContact = vec3(clearance, nearFade, farFade);
		EmitVertex();
	}
	EndPrimitive();
}

void main()
{
	if (u_WeatherType == 1)
	{
		if (var_Culled[0] == 0)
			EmitRain();
		return;
	}

	vec3 offsets[] = vec3[](
		vec3(-u_ViewInfo.x, -u_ViewInfo.y, 0.0),
		vec3( u_ViewInfo.x, -u_ViewInfo.y, 0.0),
		vec3(-u_ViewInfo.x,  u_ViewInfo.y, 0.0),
		vec3( u_ViewInfo.x,  u_ViewInfo.y, 0.0)
	);

	const vec2 texcoords[] = vec2[](
		vec2(1.0, 1.0),
		vec2(0.0, 1.0),
		vec2(1.0, 0.0),
		vec2(0.0, 0.0)
	);

	if (var_Culled[0] <= 0)
	{
		vec3 P = gl_in[0].gl_Position.xyz;
		vec3 V = u_ViewOrigin - P;
		vec2 toCamera = normalize(vec2(V.y, -V.x));
		for (int i = 0; i < 4; ++i)
		{
			vec3 offset = vec3(offsets[i].x * toCamera.xy, offsets[i].y);
			if (var_Velocity[0].z != 0.0)
				offset.xy += offset.z * var_Velocity[0].xy / var_Velocity[0].z;
			vec4 worldPos = vec4(P + offset, 1.0);
			gl_Position = u_viewProjectionMatrix * worldPos;

			float distance = distance(u_ViewOrigin, worldPos.xyz);
			float alpha = (u_ViewInfo.w - distance) / u_ViewInfo.w;
			if (var_Culled[0] < 0 && offsets[i].y < 0.0)
				alpha = 0.0;

			var_TexCoordAlpha = vec3(texcoords[i], clamp(alpha, 0.0, 1.0));
			var_RainContact = vec3(0.0);
			var_RainColor = vec4(0.0);
			var_RainDebug = vec4(0.0);
			EmitVertex();
		}
		EndPrimitive();
	}
}

/*[Fragment]*/
uniform vec4 u_Color;
uniform sampler2D u_DiffuseMap;

uniform int u_WeatherType;
uniform vec4 u_RainStreak;	// width scale, length scale, weather depth range, sRGB coverage
uniform vec4 u_RainShade;	// opacity, lighting mix, world size of a pixel at distance 1, debug

in vec3 var_TexCoordAlpha;
in vec3 var_RainContact;
flat in vec4 var_RainColor;
flat in vec4 var_RainDebug;

out vec4 out_Color;
out vec4 out_Glow;

void main()
{
	vec4 textureColor = texture(u_DiffuseMap, var_TexCoordAlpha.xy);

	if (u_WeatherType == 1)
	{
		// rain.jpg has no alpha: a gray streak on black. Its brightness is
		// the drop coverage, measured on the stored (sRGB) values.
		float coverage = max(textureColor.r, max(textureColor.g, textureColor.b));
		if (u_RainStreak.w > 0.5)
			coverage = pow(coverage, 1.0 / 2.2);

		// fade out over the last units above the rain occluder
		float clearance = var_RainContact.x;
		float contact = smoothstep(0.0, var_RainColor.w, clearance);
		float alpha = coverage * var_TexCoordAlpha.z * contact;

		// premultiplied over (ONE, ONE_MINUS_SRC_ALPHA): black texels are
		// fully transparent, lit texels add light and hide a little of the
		// background
		out_Color = vec4(var_RainColor.rgb * alpha, alpha);
		// glow alpha 0 keeps the bloom buffer behind the rain untouched
		out_Glow = vec4(0.0);

		int debugMode = int(u_RainShade.w);
		if (debugMode > 0)
		{
			vec3 debugColor = vec3(coverage);
			if (debugMode == 2)
				debugColor = vec3(1.0 - var_RainContact.y, var_RainContact.y * var_RainContact.z, 1.0 - var_RainContact.z);
			else if (debugMode == 3)
				debugColor = var_RainDebug.rgb * 0.5;
			else if (debugMode == 4)
				debugColor = clearance < 0.0 ? vec3(1.0, 0.0, 1.0) : mix(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), contact);
			else if (debugMode == 5)
				debugColor = var_RainDebug.rgb;
			// opaque over the streak so the value is readable; contact keeps
			// its hidden part (magenta) visible
			float mask = step(0.05, coverage);
			out_Color = vec4(debugColor * mask, mask);
		}
		return;
	}

	out_Color = textureColor * u_Color * var_TexCoordAlpha.z;

	out_Glow.rgb = vec3(0.0);
	out_Glow.a = out_Color.a;
}
