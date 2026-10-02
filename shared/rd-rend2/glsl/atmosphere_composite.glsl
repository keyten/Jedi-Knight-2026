/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Atmosphere composite (r_atmosphere, tr_atmosphere.cpp): the aerial perspective of everything drawn before
// it (the layers up to SS_FOG, the sky included), from the depth buffer, in the HDR scene before tone mapping
// and BEFORE the froxel composite, so a pixel ends up as
//
//   S_froxel + T_froxel * (S_atmosphere + T_atmosphere * surface)
//
// Atmospheric transmittance is coloured, so the composite is two draws (u_Atmosphere[7].z):
//   0  blend ZERO, SRC_COLOR  out = T          color = color * T, glow = glow * T
//   1  blend ONE, ONE         out = S          color += S
// Sky pixels (depth 1): overlay (mode 0) = aerial perspective over the sky distance, the Mie lobe scaled by the
// sun glow; blend (1) = mix(sky, analytic sky, blend); analytic (2) = sky-view LUT (+ the sun disc). The
// analytic sky already integrates the atmosphere to space: no aerial perspective on top of it.

uniform sampler2D u_ScreenDepthMap;
uniform mat4 u_AtmosphereInvViewProjection;

out vec4 out_Color;
out vec4 out_Glow;

// first person view model (depth hack range, as FROXEL_DEPTH_HACK_MAX)
#define ATMO_DEPTH_HACK_MAX 0.3001

vec3 AtmoSunIlluminance() { return u_Atmosphere[3].rgb; }

// physical radiance per unit illuminance -> buffer units: the buffer's sun value is E / pi (a white
// Lambert surface at normal incidence shows E_buffer, physically E / pi)
#define ATMO_RADIANCE_SCALE ATMO_PI

vec3 AtmoSunDisc(vec3 dir, float h)
{
	vec3 sunDir = u_Atmosphere[2].xyz;
	float cosRadius = u_Atmosphere[2].w;
	float c = dot(dir, sunDir);
	if (u_Atmosphere[3].w < 0.5 || c < cosRadius)
		return vec3(0.0);
	// linear limb darkening, normalized to the disc mean (1 - 0.6 / 3)
	float sinRadius2 = max(1.0 - cosRadius * cosRadius, 1e-8);
	float x2 = clamp((1.0 - c * c) / sinRadius2, 0.0, 1.0);
	float limb = (1.0 - 0.6 * (1.0 - sqrt(1.0 - x2))) / 0.8;
	// E / solid angle (pi theta^2), in buffer units E_buffer / theta^2
	vec3 L = AtmoSunIlluminance() / sinRadius2 * limb * AtmoTransmittanceToSpace(h, sunDir.z);
	float lum = dot(L, vec3(0.2126, 0.7152, 0.0722));
	return lum > ATMO_SUN_DISC_MAX ? L * (ATMO_SUN_DISC_MAX / lum) : L;
}

vec3 AtmoHeat(float x)
{
	x = clamp(x, 0.0, 1.0);
	return clamp(vec3(1.5 - abs(4.0 * x - 3.0), 1.5 - abs(4.0 * x - 2.0), 1.5 - abs(4.0 * x - 1.0)), 0.0, 1.0);
}

void main()
{
	vec2 tc = gl_FragCoord.xy / r_FBufScale;
	float depth = texture(u_ScreenDepthMap, tc).r;
	int skyMode = int(u_Atmosphere[5].x);
	int debugView = int(u_Atmosphere[5].w);
	bool radiancePass = u_Atmosphere[7].z > 0.5;

	vec2 ndc = (tc - u_Atmosphere[6].xy) / u_Atmosphere[6].zw * 2.0 - 1.0;
	vec3 viewOrigin = u_Atmosphere[4].xyz;
	bool sky = depth >= 1.0;
	vec3 dir;
	float dist;
	if (sky)
	{
		vec4 farPoint = u_AtmosphereInvViewProjection * vec4(ndc, 1.0, 1.0);
		dir = normalize(farPoint.xyz / farPoint.w - viewOrigin);
		dist = u_Atmosphere[4].w;
	}
	else
	{
		float d = depth <= ATMO_DEPTH_HACK_MAX ? depth / 0.3 : depth;
		vec4 p = u_AtmosphereInvViewProjection * vec4(ndc, d * 2.0 - 1.0, 1.0);
		vec3 toPos = p.xyz / p.w - viewOrigin;
		dist = length(toPos);
		dir = toPos / max(dist, 1e-6);
		// the view model gets no aerial perspective
		if (depth <= ATMO_DEPTH_HACK_MAX)
			dist = 0.0;
	}

	vec3 sunDir = u_Atmosphere[2].xyz;
	float hCamera = AtmoAltitude(viewOrigin.z);
	float start = min(u_Atmosphere[7].x, dist);
	float h0 = max(AtmoAltitude(viewOrigin.z + dir.z * start), -0.5);
	// world units -> km, scaled by r_atmosphereAerialScale (path only, not altitude)
	float len = (dist - start) * u_Atmosphere[1].x * 0.001 * u_Atmosphere[1].y;
	float nu = dot(dir, sunDir);
	float mieGlow = sky ? u_Atmosphere[5].z : 1.0;

	vec3 sR, sM, T;
	AtmosphereAerial(h0, dir.z, len, nu, mieGlow, sR, sM, T);
	vec3 E = AtmoSunIlluminance() * ATMO_RADIANCE_SCALE;
	vec3 S = (sR + sM) * E;

	if (debugView > 0)
	{
		vec3 debugColor = vec3(0.0);
		if (debugView == 1)
			debugColor = sR * E;
		else if (debugView == 2)
			debugColor = sM * E;
		else if (debugView == 3)
			debugColor = T;
		else if (debugView == 4)
			debugColor = S;
		else if (debugView == 5)
			debugColor = AtmoSkyView(dir) * E + AtmoSunDisc(dir, max(hCamera, 0.0));
		else if (debugView == 6)
		{
			// composition: red = froxel opacity, green = atmosphere opacity (luminance of 1 - T)
			float atmo = 1.0 - dot(T, vec3(0.2126, 0.7152, 0.0722));
			float froxel = 0.0;
#if defined(USE_FROXEL_FOG)
			if (u_Atmosphere[7].w > 0.5)
			{
				vec3 worldPos = FroxelSceneWorldPosition(tc, depth);
#if defined(USE_FROXEL_RGB)
				vec3 froxelT;
				FroxelFogRGB(worldPos, froxelT);
				froxel = 1.0 - dot(froxelT, vec3(0.2126, 0.7152, 0.0722));
#else
				froxel = 1.0 - FroxelFog(worldPos).a;
#endif
			}
#endif
			debugColor = vec3(froxel, atmo, sky ? 0.15 : 0.0);
		}
		else if (debugView == 7)
		{
			// LUT viewer: transmittance (bottom left), multiple scattering x 50 (above), sky-view (right)
			vec2 uv = (tc - u_Atmosphere[6].xy) / u_Atmosphere[6].zw;
			if (uv.x < 0.5 && uv.y < 0.25)
				debugColor = texture(u_AtmosphereTransmittanceMap, vec2(uv.x * 2.0, uv.y * 4.0)).rgb;
			else if (uv.x < 0.5 && uv.y < 0.5)
				debugColor = texture(u_AtmosphereMultiScatterMap, vec2(uv.x * 2.0, (uv.y - 0.25) * 4.0)).rgb * 50.0;
			else if (uv.x >= 0.5 && uv.y < 0.5)
				debugColor = texture(u_AtmosphereSkyViewMap, vec2((uv.x - 0.5) * 2.0, uv.y * 2.0)).rgb * E;
			else
				debugColor = vec3(0.02);
		}
		else if (debugView == 8)
		{
			// scaled aerial distance bands: 1, 2, 5, 10, 20 km and beyond, darkened by the transmittance
			float band = len < 1.0 ? 0.0 : len < 2.0 ? 0.2 : len < 5.0 ? 0.4 : len < 10.0 ? 0.6 : len < 20.0 ? 0.8 : 1.0;
			debugColor = AtmoHeat(band) * mix(0.35, 1.0, dot(T, vec3(0.3333)));
		}

		// the first draw clears, the second writes the view
		out_Color = radiancePass ? vec4(debugColor, 0.0) : vec4(0.0, 0.0, 0.0, 1.0);
		out_Glow = radiancePass ? vec4(0.0) : vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	// sky modes (sky portal maps force the overlay: the "sky" there is the portal's scene)
	vec3 keep = T;
	vec3 add = S;
	if (sky && skyMode > 0)
	{
		vec3 analytic = AtmoSkyView(dir) * E;
		if (skyMode == 1)
		{
			float b = clamp(u_Atmosphere[5].y, 0.0, 1.0);
			keep = vec3(1.0 - b);
			add = b * analytic;
		}
		else
		{
			keep = vec3(0.0);
			add = analytic + AtmoSunDisc(dir, max(hCamera, 0.0));
		}
	}

	if (radiancePass)
	{
		out_Color = vec4(add, 0.0);
		out_Glow = vec4(0.0);
	}
	else
	{
		out_Color = vec4(keep, 1.0);
		out_Glow = vec4(keep, 1.0);
	}
}
