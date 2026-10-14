/*[Geometry]*/
// Dynamic lighting shared by weather.glsl and weatherSplash.glsl. The
// renderer's Forward+ buffers remain the source of truth; the Lights block is
// the legacy/failure fallback. Work is bounded independently of scene light
// count because this runs once per emitted weather particle.

layout(std140) uniform Camera
{
	mat4 u_viewProjectionMatrix;
	vec4 _u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
	ivec4 u_FPlusGrid;
	vec4 u_FPlusParams;
	vec4 u_FPlusParams2;
	vec4 u_FPlusDebug;
};

struct WeatherLight
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
	mat4 _u_WeatherShadowMvp;
	mat4 _u_WeatherShadowMvp2;
	mat4 _u_WeatherShadowMvp3;
	vec4 _u_WeatherShadowSplits;
	vec4 _u_WeatherShadowBlend;
	vec4 _u_WeatherShadowTexelSize;
	vec4 _u_WeatherShadowDepthSpan;
	vec4 _u_WeatherShadowBias;
	vec4 _u_WeatherShadowPcss;
	vec4 _u_WeatherShadowDebug;
	int u_NumLights;
	WeatherLight u_Lights[32];
};

uniform samplerBuffer u_FPlusLights;
uniform usamplerBuffer u_FPlusGridMap;
uniform usamplerBuffer u_FPlusIndexMap;
// x: 0 current/static only, 1 strongest dynamic, 2 limited cluster lights
// y: output light limit, z: debug (1 static, 2 dynamic, 3 selected count)
// w: maximum candidates examined per particle
uniform vec4 u_RainDynamic;

#define WEATHER_FPLUS_LIGHT_TEXELS 5
#define WEATHER_FPLUS_TYPE_RECT 1.0
#define WEATHER_MAX_CANDIDATES 8

struct WeatherDynamicResult
{
	vec3 light;
	float selected;
};

float WeatherLuminance(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// Matches lightall.glsl: inverse-square metres and an edge-only range window
// in physical mode, exact Q3 finite-radius attenuation in compatibility mode.
float WeatherPointAttenuation(vec3 receiverToLight, float radius, float sourceRadius,
	vec4 spot, float cosInner)
{
	float distanceSq = max(dot(receiverToLight, receiverToLight), 1e-8);
#if defined(USE_PHYSICAL_PBR)
	float x = sqrt(distanceSq) / max(radius, 1e-4);
	float edge = clamp((x - 0.8) / 0.2, 0.0, 1.0);
	float window = 1.0 - edge * edge * (3.0 - 2.0 * edge);
	float attenuation = window / (max(distanceSq, sourceRadius * sourceRadius) *
		PBR_METERS_PER_WORLD_UNIT_SQUARED);
#else
	distanceSq = max(distanceSq, 1.0);
	float attenuation = clamp(0.5 * radius * radius / distanceSq - 0.5, 0.0, 1.0);
#endif
	if (attenuation <= 0.0)
		return 0.0;
	vec3 L = receiverToLight * inversesqrt(distanceSq);
	return attenuation * smoothstep(spot.w, cosInner, dot(-L, spot.xyz));
}

int WeatherFPlusSlice(vec3 position)
{
	int slices = int(u_FPlusParams.y);
	if (slices <= 1)
		return 0;
	float depth = dot(position - u_ViewOrigin, normalize(u_ViewForward));
	if (depth <= u_FPlusParams2.w)
		return 0;
	return clamp(1 + int(floor(log(depth) * u_FPlusParams.z + u_FPlusParams.w)), 1, slices - 1);
}

ivec2 WeatherFPlusList(vec3 position)
{
	// Geometry shaders have no gl_FragCoord. Project the particle centre and
	// address the same screen tile. The last partial tile is conservatively
	// represented by the normalized grid extent.
	vec4 clip = u_viewProjectionMatrix * vec4(position, 1.0);
	vec2 screen = clip.xy / max(abs(clip.w), 1e-6) * 0.5 + 0.5;
	ivec2 tile = clamp(ivec2(screen * vec2(u_FPlusGrid.zw)),
		ivec2(0), u_FPlusGrid.zw - ivec2(1));
	int cluster = (WeatherFPlusSlice(position) * u_FPlusGrid.w + tile.y) *
		u_FPlusGrid.z + tile.x;
	uvec4 cell = texelFetch(u_FPlusGridMap, u_FPlusGrid.x + cluster);
	return ivec2(int(cell.x), int(cell.y));
}

vec3 WeatherLightResponse(vec3 position, vec3 normal, float surfaceResponse,
	vec3 lightOrigin, vec3 lightColor, float radius, vec4 spot, float cosInner,
	float sourceRadius,
	out float importance)
{
	vec3 toLight = lightOrigin - position;
	float attenuation = WeatherPointAttenuation(toLight, radius, sourceRadius, spot, cosInner);
	vec3 L = toLight * inversesqrt(max(dot(toLight, toLight), 1.0));
	// Streaks are tiny scattering cylinders; splashes also receive a broad
	// diffuse response so crowns and sprays do not wink out as their cards turn.
	float incidence = mix(0.55 + 0.45 * abs(dot(L, normalize(position - u_ViewOrigin))),
		0.25 + 0.75 * max(dot(normal, L), 0.0), surfaceResponse);
	vec3 contribution = lightColor * (attenuation * incidence);
	importance = WeatherLuminance(contribution);
	if (importance < (1.0 / 512.0))
	{
		importance = 0.0;
		return vec3(0.0);
	}
	return contribution;
}

vec3 WeatherEvaluateFPlus(vec3 position, vec3 normal, float surfaceResponse,
	int lightIndex, out float importance)
{
	int base = u_FPlusGrid.y + lightIndex * WEATHER_FPLUS_LIGHT_TEXELS;
	vec4 t0 = texelFetch(u_FPlusLights, base);
	vec4 t1 = texelFetch(u_FPlusLights, base + 1);
	vec4 t2 = texelFetch(u_FPlusLights, base + 2);
	vec3 lightOrigin = t0.xyz;
	vec4 spot = vec4(0.0, 0.0, 0.0, -2.0);
	float cosInner = -1.0;

	if (t1.w == 0.0)
	{
		cosInner = t2.z;
		if (cosInner > -0.5)
			spot = texelFetch(u_FPlusLights, base + 3);
	}
	else
	{
		// A line saber or rectangle remains one light in Forward+. Approximate
		// its rain response from the closest point on the emitter, preserving
		// its authoritative color and influence radius.
		vec3 right = texelFetch(u_FPlusLights, base + 3).xyz;
		vec3 local = position - lightOrigin;
		lightOrigin += right * clamp(dot(local, right), -t2.z, t2.z);
		if (t1.w == WEATHER_FPLUS_TYPE_RECT)
		{
			vec3 up = texelFetch(u_FPlusLights, base + 4).xyz;
			lightOrigin += up * clamp(dot(local, up), -t2.w, t2.w);
		}
	}
	return WeatherLightResponse(position, normal, surfaceResponse, lightOrigin,
		t1.rgb, t0.w, spot, cosInner,
		t1.w == 0.0 ? texelFetch(u_FPlusLights, base + 4).y : 0.0, importance);
}

vec3 WeatherEvaluateUbo(vec3 position, vec3 normal, float surfaceResponse,
	int lightIndex, out float importance)
{
	WeatherLight light = u_Lights[lightIndex];
	return WeatherLightResponse(position, normal, surfaceResponse,
		light.origin.xyz, light.color, light.radius, light.spot, light.spot2.x,
		light.physical.x, importance);
}

WeatherDynamicResult WeatherDynamicLight(vec3 position, vec3 normal, float surfaceResponse)
{
	WeatherDynamicResult result;
	result.light = vec3(0.0);
	result.selected = 0.0;
	int mode = int(u_RainDynamic.x + 0.5);
	if (mode <= 0)
		return result;

	bool fplus = u_FPlusParams2.z > 0.0;
	ivec2 list = fplus ? WeatherFPlusList(position) : ivec2(0, u_NumLights);
	int candidates = min(min(list.y, int(u_RainDynamic.w + 0.5)), WEATHER_MAX_CANDIDATES);
	int outputLimit = mode == 1 ? 1 : clamp(int(u_RainDynamic.y + 0.5), 1, WEATHER_MAX_CANDIDATES);
	float bestImportance = 0.0;
	vec3 best = vec3(0.0);
	for (int k = 0; k < WEATHER_MAX_CANDIDATES; ++k)
	{
		if (k >= candidates)
			break;
		int lightIndex = fplus ? int(texelFetch(u_FPlusIndexMap, list.x + k).x) : k;
		float importance;
		vec3 contribution;
		if (fplus)
			contribution = WeatherEvaluateFPlus(position, normal, surfaceResponse,
				lightIndex, importance);
		else
			contribution = WeatherEvaluateUbo(position, normal, surfaceResponse,
				lightIndex, importance);
		if (importance <= 0.0)
			continue;
		if (mode == 1)
		{
			if (importance > bestImportance)
			{
				bestImportance = importance;
				best = contribution;
			}
		}
		else if (int(result.selected) < outputLimit)
		{
			result.light += contribution;
			result.selected += 1.0;
		}
	}
	if (mode == 1 && bestImportance > 0.0)
	{
		result.light = best;
		result.selected = 1.0;
	}
	return result;
}

vec3 WeatherApplyDynamicLight(vec3 staticLight, vec3 position, vec3 normal,
	float surfaceResponse)
{
	WeatherDynamicResult dynamic = WeatherDynamicLight(position, normal, surfaceResponse);
	int mode = int(u_RainDynamic.x + 0.5);
	int debugMode = int(u_RainDynamic.z + 0.5);
	if (debugMode == 1)
		return staticLight;
	if (debugMode == 2)
		return dynamic.light;
	if (debugMode == 3)
	{
		float limit = max(mode == 1 ? 1.0 : u_RainDynamic.y, 1.0);
		float count = clamp(dynamic.selected / limit, 0.0, 1.0);
		return mix(vec3(0.02, 0.02, 0.08), vec3(0.1, 1.0, 0.2), count);
	}
	return staticLight + dynamic.light;
}
