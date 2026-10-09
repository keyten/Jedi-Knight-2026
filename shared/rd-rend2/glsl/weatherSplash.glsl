/*[Vertex]*/
// r_rainSplashes: one point per rain particle of a chunk slot. The impact
// state is written by weatherUpdate.glsl; the geometry shader drops every
// particle without a live impact.
uniform vec2 u_ZoneOffset[9];

in vec3 attr_Position;
in vec3 attr_Color;		// velocity
in vec4 attr_TexCoord0;	// impact: world xyz, state (weatherUpdate.glsl)

out vec4 var_Impact;
out vec3 var_Velocity;
out int var_Id;

#if defined(USE_LIQUIDS)
// r_volumetricWater (tr_liquid.cpp RB_LiquidWeatherSetupDraw): no weather inside a liquid brush.
// The Liquids block of liquid_common.glsl (the same layout, a fragment library there); only the
// brush count, bounds and planes are read here.
layout(std140) uniform Liquids
{
	vec4 u_LiquidParams;		// x: brushes in the block
	vec4 u_LiquidCaustics;
	vec4 u_LiquidView;
	vec4 u_LiquidCausticSurface;
	vec4 u_LiquidCausticDebug;
	vec4 u_LiquidCausticLights;
	vec4 u_LiquidMaterial[6];
	vec4 u_LiquidMins[MAX_GPU_LIQUIDS];	// bounds, w: first plane
	vec4 u_LiquidMaxs[MAX_GPU_LIQUIDS];	// w: planes + 64 * medium + 256 * class
	vec4 u_LiquidWaveParams[MAX_GPU_LIQUIDS * 8];
	vec4 u_LiquidWaveTerms[MAX_GPU_LIQUIDS * 8];
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

void main()
{
	// the particle itself, only read by the debug views
	gl_Position = vec4(attr_Position.xy + u_ZoneOffset[0], attr_Position.z, 1.0);
	var_Velocity = attr_Color;
	var_Impact = attr_TexCoord0;
	var_Id = gl_VertexID;
#if defined(USE_LIQUIDS)
	// an impact under a liquid surface (on the bed: the weather occlusion map has
	// no water) is spent; the point just above the hit tests the water over it
	if (var_Impact.w > -1.5 && WeatherInLiquid(var_Impact.xyz + vec3(0.0, 0.0, 2.0)))
		var_Impact.w = -2.0;
#endif
}

/*[Geometry]*/
layout(points) in;
layout(triangle_strip, max_vertices = 8) out;

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

uniform vec4 u_Color;			// weather tint (legacy color, rgb * a)
uniform mat4 u_WeatherMvp;
uniform sampler2D u_ShadowMap;	// weather depth
uniform vec4 u_SplashParams;	// size (world units), opacity, fade distance, debug mode
uniform vec4 u_SplashParams2;	// weather map texel (world units), depth range (world units), frame time (ms), 1 / lifetime (1/ms)
uniform vec4 u_RainLight;		// rgb = light without a grid, w = light grid valid
uniform sampler3D u_VolumetricLightMap;
uniform vec3 u_LightGridOrigin;
uniform vec3 u_LightGridCellInverseSize;

in vec4 var_Impact[];
in vec3 var_Velocity[];
in int var_Id[];

out vec2 var_Local;
flat out vec4 var_Shape;	// kind (0 crown, 1 spray, 2 debug), age, alpha, seed
flat out vec3 var_Light;	// premultiplied colour scale (debug: the colour)
flat out vec2 var_Extent;	// spray: world width, height

const float PI = 3.14159265;

uint HashUint(uint x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

float WeatherDepth(vec3 position)
{
	return (u_WeatherMvp * vec4(position, 1.0)).z * 0.5 + 0.5;
}

float SurfaceDepth(vec3 position)
{
	vec2 uv = (u_WeatherMvp * vec4(position, 1.0)).xy * 0.5 + 0.5;
	return textureLod(u_ShadowMap, uv, 0.0).r;
}

// Upward normal from the height gradient of the weather depth. The D16 map
// is coarse: the tilt is limited to 45 degrees.
vec3 SurfaceNormal(vec3 position)
{
	float e = u_SplashParams2.x;
	float scale = u_SplashParams2.y / (2.0 * e);
	// the height is -depth * range, so dh/dx = (d(x - e) - d(x + e)) * range / 2e
	vec2 slope = vec2(
		SurfaceDepth(position - vec3(e, 0.0, 0.0)) - SurfaceDepth(position + vec3(e, 0.0, 0.0)),
		SurfaceDepth(position - vec3(0.0, e, 0.0)) - SurfaceDepth(position + vec3(0.0, e, 0.0))) * scale;
	float len = length(slope);
	if (len > 1.0)
		slope /= len;
	return normalize(vec3(-slope, 1.0));
}

vec3 SplashLight(vec3 position)
{
	vec3 light = u_RainLight.rgb;
	if (u_RainLight.w > 0.5)
	{
		vec3 gridSize = vec3(textureSize(u_VolumetricLightMap, 0));
		vec3 cell = (position - u_LightGridOrigin) * u_LightGridCellInverseSize;
		light = texture(u_VolumetricLightMap, (cell + 0.5) / gridSize).rgb;
	}
	return light;
}

void EmitQuad(vec3 origin, vec3 axisX, vec3 axisY, vec2 lo, vec2 hi,
	vec4 shape, vec3 light, vec2 extent)
{
	const vec2 corners[] = vec2[](
		vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
	for (int i = 0; i < 4; ++i)
	{
		vec2 local = mix(lo, hi, corners[i]);
		gl_Position = u_viewProjectionMatrix * vec4(origin + axisX * local.x + axisY * local.y, 1.0);
		var_Local = local;
		var_Shape = shape;
		var_Light = light;
		var_Extent = extent;
		EmitVertex();
	}
	EndPrimitive();
}

// camera facing marker of a roughly constant screen size
void EmitMarker(vec3 position, float pixels, vec3 color)
{
	float size = max(0.5, distance(u_ViewOrigin, position) * 0.0015 * pixels);
	EmitQuad(position, u_ViewLeft * size, u_ViewUp * size, vec2(-1.0), vec2(1.0),
		vec4(2.0, 0.0, 1.0, 0.0), color, vec2(0.0));
}

// r_rainSplashDebug 2 (crossing test) and 3 (trajectories), every particle
void EmitDebugParticle(int debugMode)
{
	vec3 p1 = gl_in[0].gl_Position.xyz;
	if (distance(u_ViewOrigin, p1) > u_SplashParams.z)
		return;

	// the update integrated p1 = p0 + v * dt with the stored velocity; wrap
	// and respawn frames are not reconstructed
	vec3 p0 = p1 - var_Velocity[0] * u_SplashParams2.z;
	float surface = SurfaceDepth(p1);
	float d0 = WeatherDepth(p0) - surface;
	float d1 = WeatherDepth(p1) - surface;
	bool under = d1 > 0.0;

	if (debugMode == 3)
	{
		// the last 50 ms of the path, green above the occluder, red under it
		vec3 tail = p1 - var_Velocity[0] * 50.0;
		vec3 along = p1 - tail;
		vec3 side = normalize(cross(along + vec3(0.0, 0.0, 1e-4), p1 - u_ViewOrigin));
		float width = max(0.3, distance(u_ViewOrigin, p1) * 0.0015);
		EmitQuad(tail, side * width, along, vec2(-1.0, 0.0), vec2(1.0, 1.0),
			vec4(2.0, 0.0, 1.0, 0.0), under ? vec3(0.8, 0.1, 0.1) : vec3(0.1, 0.8, 0.2), vec2(0.0));
		return;
	}

	if (d0 <= 0.0 && d1 > 0.0)
	{
		// crossing this frame: cyan accepted (a fresh impact of this particle
		// near it), magenta rejected (edge, brush or tolerance test, or the
		// drop already hit in this fall)
		vec4 impact = var_Impact[0];
		bool fresh = impact.w > 1.0 - 1.5 * u_SplashParams2.z * u_SplashParams2.w &&
			distance(impact.xy, p1.xy) < 64.0;
		EmitMarker(p1, 4.0, fresh ? vec3(0.0, 1.0, 1.0) : vec3(1.0, 0.0, 1.0));
		return;
	}
	EmitMarker(p1, 1.5, under ? vec3(0.5, 0.05, 0.05) : vec3(0.05, 0.5, 0.1));
}

void main()
{
	int debugMode = int(u_SplashParams.w + 0.5);
	if (debugMode >= 2)
	{
		EmitDebugParticle(debugMode);
		return;
	}

	// impact state of weatherUpdate.glsl: |w| is the life, w < -1.5 a spent splash
	vec4 impact = var_Impact[0];
	float life = impact.w > -1.5 ? abs(impact.w) : 0.0;
	if (life <= 0.0 || life > 1.0)
		return;

	vec3 hit = impact.xyz;
	float fadeDistance = u_SplashParams.z;
	float viewDistance = distance(u_ViewOrigin, hit);
	float fade = 1.0 - smoothstep(0.6 * fadeDistance, fadeDistance, viewDistance);
	if (fade <= 0.0)
		return;

	if (debugMode == 1)
	{
		// impact points, yellow when new, red when about to die
		EmitMarker(hit, 3.0, mix(vec3(1.0, 0.0, 0.0), vec3(1.0, 1.0, 0.0), life));
		return;
	}

	// GLSL 1.50 has no floatBitsToUint: the impact spot at 1/8 unit
	uvec2 spot = uvec2(ivec2(floor(hit.xy * 8.0)));
	uint h = HashUint(uint(var_Id[0]) * 747796405u ^ spot.x ^ (spot.y * 2891336453u));
	vec4 rand = vec4(uvec4(h, h >> 8, h >> 16, h >> 24) & uvec4(255u)) / 255.0;

	float age = 1.0 - life;
	float size = u_SplashParams.x * mix(0.75, 1.25, rand.x);
	vec3 n = SurfaceNormal(hit);

	// water takes the light around it; the weather tint keeps acid rain green
	vec3 tint = u_Color.rgb / max(u_Color.a, 1e-4);
	vec3 light = SplashLight(hit + n * 4.0) * tint * 0.8;
	float opacity = u_SplashParams.y * fade;

	// crown: a ring spreading in the surface plane
	vec3 axis = abs(n.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 t = normalize(cross(axis, n));
	vec3 b = cross(n, t);
	float angle = rand.y * 2.0 * PI;
	vec3 tangent = cos(angle) * t + sin(angle) * b;
	vec3 bitangent = cross(n, tangent);
	float radius = size * (0.35 + 0.65 * sqrt(age));
	EmitQuad(hit + n * 0.5, tangent * radius, bitangent * radius, vec2(-1.0), vec2(1.0),
		vec4(0.0, age, opacity, rand.z), light, vec2(radius));

	// spray: a short camera facing sheet along the normal, up and down again;
	// a few pixels at most beyond half the fade distance, the crown alone
	// carries the splash there
	float height = size * 1.4 * sin(PI * sqrt(age)) * mix(0.6, 1.4, rand.w);
	if (height > 0.3 && viewDistance < 0.5 * fadeDistance)
	{
		vec3 toCamera = u_ViewOrigin - hit;
		vec3 side = cross(n, toCamera);
		float sideLength = length(side);
		side = sideLength > 1e-3 ? side / sideLength : tangent;
		float width = size * 0.35;
		EmitQuad(hit + n * 0.3, side * width, n * height, vec2(-1.0, 0.0), vec2(1.0),
			vec4(1.0, age, opacity, rand.z), light, vec2(width, height));
	}
}

/*[Fragment]*/
in vec2 var_Local;
flat in vec4 var_Shape;
flat in vec3 var_Light;
flat in vec2 var_Extent;

out vec4 out_Color;
out vec4 out_Glow;

void main()
{
	int kind = int(var_Shape.x + 0.5);
	// glow alpha 0 keeps the bloom buffer behind the splash untouched
	out_Glow = vec4(0.0);
	if (kind == 2)
	{
		out_Color = vec4(var_Light, 1.0);
		return;
	}

	float age = var_Shape.y;
	float seed = var_Shape.w;
	float alpha;
	if (kind == 0)
	{
		// thin ring that widens and thins out, over a faint wet disc
		float r = length(var_Local);
		float width = mix(0.08, 0.25, age);
		float ring = 1.0 - smoothstep(0.0, width, abs(r - 0.8));
		float disc = (1.0 - smoothstep(0.0, 0.8, r)) * (1.0 - age) * 0.3;
		alpha = max(ring, disc) * (1.0 - smoothstep(0.95, 1.0, r)) * (1.0 - age);
	}
	else
	{
		// thin central streak plus three droplets, in world units
		float streak = exp(-var_Local.x * var_Local.x * 30.0) *
			smoothstep(0.0, 0.15, var_Local.y) * (1.0 - var_Local.y) * 0.6;
		vec2 p = var_Local * var_Extent;
		float radius = var_Extent.x * 0.2;
		float drops = 0.0;
		for (int i = 0; i < 3; ++i)
		{
			float fi = float(i);
			vec2 c = vec2(
				(fract(seed * (13.1 + fi * 7.7) + fi * 0.37) - 0.5) * 1.4,
				0.35 + 0.55 * fract(seed * (5.3 + fi * 11.9) + fi * 0.61)) * var_Extent;
			drops = max(drops, 1.0 - smoothstep(radius * 0.4, radius, length(p - c)));
		}
		alpha = max(streak, drops) * (1.0 - age * age);
	}

	alpha = clamp(alpha * var_Shape.z, 0.0, 1.0);
	// premultiplied over (ONE, ONE_MINUS_SRC_ALPHA)
	out_Color = vec4(var_Light * alpha, alpha);
}
