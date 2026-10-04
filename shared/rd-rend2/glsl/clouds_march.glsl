/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Cloud ray march (r_clouds, tr_clouds.cpp): one ray per reduced resolution pixel through the cloud shell
// (clouds_common.glsl), only where the full resolution depth sees sky. Single scattering of the sun with a short
// light march for the self shadow, a few multiple scattering octaves (Wrenninge 2013 / Hillaire 2016: extinction,
// scattering and phase g scaled by 0.5 per octave), an isotropic ambient term from the sky, exact per step
// integration (energy conserving): S += T L (1 - exp(-sigma dt)), T *= exp(-sigma dt).
//
// out_Color: in-scattered radiance (rgb, buffer units, premultiplied), transmittance (a)
// out_Glow:  r = transmittance weighted cloud distance (km; CLOUD_FAR_DISTANCE without cloud, CLOUD_NO_SKY
//            where the pixel has no sky), the resolve reprojects with it
// Debug views (u_Cloud[16].x) write their colour with a = 0.

uniform sampler2D u_ScreenDepthMap;
uniform mat4 u_CloudInvViewProjection;	// clip -> world offset from the camera (no translation)

out vec4 out_Color;
out vec4 out_Glow;

#define CLOUD_MAX_STEPS 256
#define CLOUD_MAX_LIGHT_STEPS 16
#define CLOUD_MAX_OCTAVES 4
// the march stops when the background is this occluded
#define CLOUD_MIN_TRANSMITTANCE 0.01

// interleaved gradient noise (Jimenez 2014), animated per frame
float CloudIGN(vec2 p, float frame)
{
	p += 5.588238 * mod(frame, 64.0);
	return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

bool CloudAtmosphere()
{
	return u_Cloud[7].w > 0.5;
}

// sun illuminance (buffer units) at altitude h of the cloud layer
vec3 CloudSunIlluminance(float h, vec3 sunDir)
{
	if (CloudAtmosphere())
		return u_Atmosphere[3].rgb * AtmoTransmittanceToSpace(u_Cloud[9].w + h, sunDir.z);
	// without the atmosphere: the map's sun, faded out as it sets
	return u_Cloud[7].rgb * smoothstep(-0.05, 0.05, sunDir.z);
}

// Isotropic ambient radiance at the top (rgb) of the layer, and the ground bounce under it
void CloudAmbient(vec3 sunDir, out vec3 sky, out vec3 ground)
{
	if (CloudAtmosphere())
	{
		// mean sky radiance over the upper hemisphere: the zenith and a ring at 25 degrees
		vec3 E = u_Atmosphere[3].rgb * CLOUD_PI;
		vec3 ring = vec3(0.0);
		for (int i = 0; i < 6; i++)
		{
			float a = float(i) * (CLOUD_PI / 3.0);
			ring += AtmoSkyView(vec3(cos(a) * 0.906, sin(a) * 0.906, 0.423));
		}
		sky = (AtmoSkyView(vec3(0.0, 0.0, 1.0)) * 0.25 + ring * (0.75 / 6.0)) * E;
		// ground albedo 0.3, lit by the sun at the ground: radiance albedo * E_buffer * cos
		vec3 sunGround = u_Atmosphere[3].rgb * AtmoTransmittanceToSpace(u_Cloud[9].w, sunDir.z);
		ground = 0.3 * sunGround * max(sunDir.z, 0.0);
	}
	else
	{
		sky = u_Cloud[8].rgb;
		ground = u_Cloud[9].rgb;
	}
	sky *= u_Cloud[8].w;
	ground *= u_Cloud[8].w;
}

// optical depth (1/km * km) towards the sun from p: quadratically spaced taps over the light march length, the
// first two with erosion, plus one long tap beyond; the coverage / type of the sample are reused
float CloudSunOpticalDepth(vec3 p, vec3 sunDir, float coverage, float type)
{
#if defined(CLOUD_TEST_NO_SHADOW)
	// tools/rend2/test_clouds_gl.py: no self shadow (never defined in the game)
	return 0.0;
#endif
	int n = int(u_Cloud[6].w);
	float len = u_Cloud[5].w;
	float tau = 0.0;
	float previous = 0.0;
	for (int k = 0; k < CLOUD_MAX_LIGHT_STEPS; k++)
	{
		if (k >= n)
			break;
		float x = float(k + 1) / float(n);
		float edge = len * x * x;
		float dt = edge - previous;
		vec3 q = p + sunDir * (0.5 * (previous + edge));
		previous = edge;
		tau += CloudDensityCovered(q, CloudAltitude(q), CloudLods(dt), k < 2, coverage, type).extinction * dt;
	}
	vec3 q = p + sunDir * (2.0 * len);
	tau += CloudDensityCovered(q, CloudAltitude(q), CloudLods(2.0 * len), false, coverage, type).extinction * 2.0 * len;
	return tau;
}

void main()
{
	int debugView = int(u_Cloud[16].x);
	vec2 tc = CloudLowToTc(gl_FragCoord.xy);

	// sky mask: any of the full resolution depth samples under this pixel is sky
	vec2 footprint = u_Cloud[14].zw / u_Cloud[15].zw;
	bool sky = textureLod(u_ScreenDepthMap, tc, 0.0).r >= 1.0;
	if (!sky && u_Cloud[16].w > 1.0)
	{
		sky = textureLod(u_ScreenDepthMap, tc + footprint * vec2(-0.3, -0.3), 0.0).r >= 1.0 ||
			textureLod(u_ScreenDepthMap, tc + footprint * vec2( 0.3, -0.3), 0.0).r >= 1.0 ||
			textureLod(u_ScreenDepthMap, tc + footprint * vec2(-0.3,  0.3), 0.0).r >= 1.0 ||
			textureLod(u_ScreenDepthMap, tc + footprint * vec2( 0.3,  0.3), 0.0).r >= 1.0;
	}
	if (!sky)
	{
		out_Color = vec4(0.0, 0.0, 0.0, 1.0);
		out_Glow = vec4(CLOUD_NO_SKY, 0.0, 0.0, 0.0);
		return;
	}

	// view ray (world = cloud space directions)
	vec2 ndc = (tc - u_Cloud[14].xy) / u_Cloud[14].zw * 2.0 - 1.0;
	vec4 nearPoint = u_CloudInvViewProjection * vec4(ndc, -1.0, 1.0);
	vec4 farPoint = u_CloudInvViewProjection * vec4(ndc, 1.0, 1.0);
	vec3 dir = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
	vec3 sunDir = u_Cloud[6].xyz;
	vec3 origin = u_Cloud[10].xyz;

	float t0, t1;
	bool hit = CloudInterval(origin, dir, t0, t1);
	if (debugView == 1)
	{
		// interval: red = entry, green = length (both / max distance)
		float maxD = max(u_Cloud[4].w, 1e-3);
		out_Color = vec4(hit ? vec3(t0 / maxD, (t1 - t0) / maxD * 4.0, 0.0) : vec3(0.0, 0.0, 0.25), 0.0);
		out_Glow = vec4(hit ? t0 : CLOUD_FAR_DISTANCE, 0.0, 0.0, 0.0);
		return;
	}
	if (!hit)
	{
		out_Color = vec4(0.0, 0.0, 0.0, 1.0);
		out_Glow = vec4(CLOUD_FAR_DISTANCE, 0.0, 0.0, 0.0);
		return;
	}

	float len = t1 - t0;
	int steps = int(clamp(ceil(len / max(u_Cloud[11].x, 1e-4)), u_Cloud[11].y, u_Cloud[10].w));
	float dt = len / float(steps);
	float jitter = CloudIGN(gl_FragCoord.xy, u_Cloud[16].z);
	vec3 lods = CloudLods(dt);

	float mu = dot(dir, sunDir);
	int octaves = int(clamp(u_Cloud[5].z, 1.0, float(CLOUD_MAX_OCTAVES)));
	float phases[CLOUD_MAX_OCTAVES];
	{
		float gScale = 1.0;
		for (int o = 0; o < CLOUD_MAX_OCTAVES; o++)
		{
			phases[o] = CloudPhase(mu, gScale);
			gScale *= 0.5;
		}
	}
	vec3 ambientSky, ambientGround;
	CloudAmbient(sunDir, ambientSky, ambientGround);

	vec3 S = vec3(0.0);
	float T = 1.0;
	float distanceSum = 0.0;
	float weightSum = 0.0;
	// debug views: the first sample inside a cloud
	bool first = true;
	vec4 debugValue = vec4(0.0);
	float maxExtinction = 0.0;

	for (int i = 0; i < CLOUD_MAX_STEPS; i++)
	{
		if (i >= steps)
			break;
		float t = t0 + (float(i) + jitter) * dt;
		vec3 p = origin + dir * t;
		float h = CloudAltitude(p);
		CloudDensitySample s = CloudDensity(p, h, lods, true);
		if (debugView == 9 || debugView == 10)
		{
			if (first)
			{
				vec2 weather = CloudWeather(p.xy, lods.z);
				debugValue = debugView == 9 ? vec4(CloudLegacyMask(p.xy), weather.r, u_Cloud[13].x * 0.5, 1.0) :
					vec4(weather, 0.0, 1.0);
				first = false;
			}
			continue;
		}
		if (s.extinction <= 0.0)
			continue;

		float sigma = s.extinction;
		maxExtinction = max(maxExtinction, sigma);
		float tau = CloudSunOpticalDepth(p, sunDir, s.coverage, s.type);
		if (first)
		{
			first = false;
			if (debugView == 3)
				debugValue = vec4(vec3(s.low), 1.0);
			else if (debugView == 4)
				debugValue = vec4(vec3(s.high), 1.0);
			else if (debugView == 5)
				debugValue = vec4(vec3(exp(-tau)), 1.0);
		}

		// sun: multiple scattering octaves
		vec3 E = CloudSunIlluminance(h, sunDir) * CLOUD_PI;
		float a = 1.0;
		float b = 1.0;
		float sun = 0.0;
		for (int o = 0; o < CLOUD_MAX_OCTAVES; o++)
		{
			if (o >= octaves)
				break;
			sun += b * phases[o] * exp(-a * tau);
			a *= 0.5;
			b *= 0.5;
		}
		float hf = clamp(CloudHeightFraction(h), 0.0, 1.0);
		vec3 ambient = ambientSky * mix(0.5, 1.0, hf) + ambientGround * (1.0 - hf) * 0.5;
		vec3 L = E * sun + ambient;

		float st = exp(-sigma * dt);
		float absorbed = T * (1.0 - st);
		S += L * absorbed;
		distanceSum += t * absorbed;
		weightSum += absorbed;
		T *= st;
		if (T < CLOUD_MIN_TRANSMITTANCE)
			break;
	}

	float cloudDistance = weightSum > 1e-5 ? distanceSum / weightSum : 0.5 * (t0 + t1);

	// fade out towards the max distance (no hard edge at the horizon)
	float fade = 1.0 - smoothstep(u_Cloud[4].w * 0.7, u_Cloud[4].w, cloudDistance);
	S *= fade;
	T = mix(1.0, T, fade);

	out_Glow = vec4(cloudDistance, 0.0, 0.0, 0.0);
	if (debugView > 0 && debugView != 7 && debugView != 11)
	{
		vec3 c = vec3(0.0);
		if (debugView == 2)
			c = vec3(maxExtinction / max(u_Cloud[1].x, 1e-4));
		else if (debugView == 3 || debugView == 4 || debugView == 5)
			c = first ? vec3(0.0, 0.0, 0.25) : debugValue.rgb;
		else if (debugView == 6)
			c = CloudHeat(phases[0] * 4.0 * CLOUD_PI / 4.0);
		else if (debugView == 8)
			c = S;
		else if (debugView == 9 || debugView == 10)
			c = debugValue.rgb;
		out_Color = vec4(c, 0.0);
		return;
	}
	out_Color = vec4(S, T);
}
